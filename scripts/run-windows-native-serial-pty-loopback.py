#!/usr/bin/env python3
import errno
import fcntl
import os
import re
import select
import signal
import subprocess
import sys
import termios
import time
import tty
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path


ORACLES = (
    (
        bytes.fromhex("01 03 00 00 00 02 C4 0B"),
        bytes.fromhex("01 03 04 41 48 00 00 7B F3"),
    ),
    (
        bytes.fromhex("01 03 00 02 00 02 65 CB"),
        bytes.fromhex("01 03 04 12 34 56 78 81 07"),
    ),
)
CANCEL_PENDING_MARKER = bytes.fromhex("7E 43 41 4E 43 45 4C 7F")
VALID_SCENARIOS = {"normal", "reopen", "timeout", "cancel", "stress", "close", "stale"}
DEFAULT_SCENARIOS = "normal,reopen,timeout,cancel,stress,close,stale"
COM_NAME_PATTERN = re.compile(r"COM([1-9][0-9]{0,2})", re.IGNORECASE)
MAX_CAPTURE_BYTES = 512 * 1024
MAX_FAULT_WAIT_MS = 500
SETUP_FAILURE_CONTEXT = "scenario=setup transaction=n/a child-exit=not-started"
LOCAL_ONLY_GATE_NOTICE = (
    "python serial matrix gate classification: local-only release-candidate evidence; "
    "Windows GitHub Actions package workflow records this requirement but does not execute POSIX PTY scenarios"
)
ACTIVE_PROCESSES = set()


@dataclass(frozen=True)
class LoopbackControls:
    timeout_ms: int
    cancel_wait_ms: int
    close_wait_ms: int
    stale_wait_ms: int
    trace: bool


def positive_int_env(name: str, default: int, maximum: int):
    value = os.environ.get(name)
    if value is None or value == "":
        return default
    try:
        parsed = int(value, 10)
    except ValueError:
        print(
            f"{name} must be an integer in [1, {maximum}], got: {value} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return None
    if parsed < 1 or parsed > maximum:
        print(
            f"{name} must be an integer in [1, {maximum}], got: {value} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return None
    return parsed


def env_has_value(name: str) -> bool:
    return os.environ.get(name) not in (None, "")


def scenario_list_env(name: str, default: str):
    value = os.environ.get(name, default)
    scenarios = [part.strip().lower() for part in value.split(",") if part.strip()]
    if not scenarios:
        print(f"{name} must contain at least one scenario {SETUP_FAILURE_CONTEXT}", file=sys.stderr)
        return None
    invalid = [scenario for scenario in scenarios if scenario not in VALID_SCENARIOS]
    if invalid:
        print(
            f"{name} has invalid scenario(s): {', '.join(invalid)}; "
            f"valid={','.join(sorted(VALID_SCENARIOS))} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return None
    duplicates = sorted({scenario for scenario in scenarios if scenarios.count(scenario) > 1})
    if duplicates:
        print(
            f"{name} has duplicate scenario(s): {', '.join(duplicates)} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return None
    return scenarios


def canonical_com_name(value: str):
    match = COM_NAME_PATTERN.fullmatch(value)
    if match is None:
        print(
            f"SVM_SERIAL_LOOPBACK_COM must be COM1..COM256, got: {value} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return None
    port_number = int(match.group(1), 10)
    if port_number > 256:
        print(
            f"SVM_SERIAL_LOOPBACK_COM must be COM1..COM256, got: {value} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return None
    return f"COM{port_number}"


def invalidate_serial_summary(path: str | None) -> bool:
    if not path:
        return True
    summary_path = Path(path)
    try:
        if summary_path.exists() or summary_path.is_symlink():
            summary_path.unlink()
    except OSError as exc:
        print(
            f"cannot invalidate serial summary path={summary_path}: {exc} {SETUP_FAILURE_CONTEXT}",
            file=sys.stderr,
        )
        return False
    return True


def write_serial_summary(path: str | None, lines: list[str]) -> None:
    if not path:
        return
    summary_path = Path(path)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = summary_path.with_name(f".{summary_path.name}.{os.getpid()}.tmp")
    try:
        temporary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        os.replace(temporary_path, summary_path)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass


def read_exact(fd: int, count: int, timeout: float) -> bytes:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while len(data) < count and time.monotonic() < deadline:
        remaining = max(0.0, deadline - time.monotonic())
        readable, _, _ = select.select([fd], [], [], min(remaining, 0.1))
        if not readable:
            continue
        try:
            chunk = os.read(fd, count - len(data))
        except OSError as exc:
            if exc.errno == errno.EIO:
                time.sleep(0.05)
                continue
            raise
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_buffered(fd: int, quiet_timeout: float = 0.05) -> bytes:
    data = bytearray()
    while True:
        readable, _, _ = select.select([fd], [], [], quiet_timeout)
        if not readable:
            return bytes(data)
        try:
            chunk = os.read(fd, 4096)
        except OSError as exc:
            if exc.errno == errno.EIO:
                return bytes(data)
            raise
        if not chunk:
            return bytes(data)
        data.extend(chunk)
        if len(data) > MAX_CAPTURE_BYTES:
            raise RuntimeError(f"captured wire bytes exceed bound={MAX_CAPTURE_BYTES}")


def write_all(fd: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = os.write(fd, data[offset:])
        if written == 0:
            raise OSError("PTY write returned zero bytes")
        offset += written


def print_process_output(stdout: bytes, stderr: bytes) -> None:
    if stdout:
        print(stdout.decode(errors="replace"), end="")
    if stderr:
        print(stderr.decode(errors="replace"), end="", file=sys.stderr)


def finish_process(process: subprocess.Popen, timeout: float, scenario: str, com_name: str | None = None):
    try:
        stdout, stderr = process.communicate(timeout=timeout)
        ACTIVE_PROCESSES.discard(process)
        return process.returncode, stdout, stderr, False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            stdout, stderr = process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            stdout, stderr = b"", b""
        if process.poll() is not None:
            ACTIVE_PROCESSES.discard(process)
        com_context = f" com={com_name}" if com_name else ""
        print(
            f"serial child failed scenario={scenario} transaction=n/a child-exit=124 "
            f"timeout=true{com_context}",
            file=sys.stderr,
        )
        return 124, stdout, stderr, True


def terminate_active_processes() -> None:
    processes = list(ACTIVE_PROCESSES)
    for process in processes:
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    for process in processes:
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            pass
        ACTIVE_PROCESSES.discard(process)


def exit_on_signal(signum, _frame) -> None:
    raise SystemExit(128 + signum)


def launch_process(
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    scenario: str,
    iterations: int,
    reopen_count: int,
    controls: LoopbackControls,
):
    env = os.environ.copy()
    env["WINEPREFIX"] = str(wineprefix)
    env["SVM_NATIVE_SERIAL_LOOPBACK_PORT"] = com_name
    env["SVM_NATIVE_SERIAL_LOOPBACK_SCENARIO"] = scenario
    env["SVM_NATIVE_SERIAL_LOOPBACK_ITERATIONS"] = str(iterations)
    env["SVM_NATIVE_SERIAL_LOOPBACK_REOPEN_COUNT"] = str(reopen_count)
    env["SVM_NATIVE_SERIAL_LOOPBACK_TIMEOUT_MS"] = str(controls.timeout_ms)
    env["SVM_NATIVE_SERIAL_LOOPBACK_CANCEL_WAIT_MS"] = str(controls.cancel_wait_ms)
    env["SVM_NATIVE_SERIAL_LOOPBACK_CLOSE_WAIT_MS"] = str(controls.close_wait_ms)
    env["SVM_NATIVE_SERIAL_LOOPBACK_STALE_WAIT_MS"] = str(controls.stale_wait_ms)
    if controls.trace:
        env["SVM_NATIVE_SERIAL_LOOPBACK_TRACE"] = "1"
    process = subprocess.Popen(
        ["wine", str(exe_path)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=False,
        start_new_session=True,
    )
    ACTIVE_PROCESSES.add(process)
    return process


def initialize_wineprefix(wineprefix: Path, timeout_seconds: int, com_name: str) -> bool:
    if (wineprefix / "dosdevices" / "c:").exists():
        return True

    wineprefix.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["WINEPREFIX"] = str(wineprefix)
    process = subprocess.Popen(
        ["wineboot", "-u"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=False,
        start_new_session=True,
    )
    ACTIVE_PROCESSES.add(process)
    returncode, stdout, stderr, timed_out = finish_process(
        process,
        timeout_seconds,
        "wineboot",
        com_name,
    )
    if returncode != 0:
        print(
            f"wineboot failed scenario=setup transaction=n/a child-exit={returncode} "
            f"timeout={str(timed_out).lower()} prefix={wineprefix} com={com_name}",
            file=sys.stderr,
        )
        print_process_output(stdout, stderr)
        return False
    return True


@contextmanager
def temporary_com_mapping(link_path: Path, pty_path: str):
    previous_target = None
    if link_path.is_symlink():
        previous_target = os.readlink(link_path)
        link_path.unlink()
    elif link_path.exists():
        raise RuntimeError(f"refusing to replace non-symlink COM mapping: {link_path}")

    try:
        link_path.symlink_to(pty_path)
    except Exception:
        if previous_target is not None:
            link_path.symlink_to(previous_target)
        raise

    try:
        yield
    finally:
        if link_path.is_symlink():
            if os.readlink(link_path) != pty_path:
                raise RuntimeError(f"COM mapping changed during matrix run: {link_path}")
            link_path.unlink()
        elif link_path.exists():
            raise RuntimeError(f"refusing to replace non-symlink COM mapping during cleanup: {link_path}")
        if previous_target is not None:
            link_path.symlink_to(previous_target)


def acquire_matrix_lock(wineprefix: Path, com_name: str) -> int:
    lock_path = wineprefix / ".svm-serial-pty-matrix.lock"
    lock_fd = os.open(lock_path, os.O_CREAT | os.O_RDWR, 0o600)
    try:
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        os.close(lock_fd)
        raise RuntimeError(f"serial PTY matrix already running prefix={wineprefix} com={com_name}")
    return lock_fd


def release_matrix_lock(lock_fd: int) -> None:
    fcntl.flock(lock_fd, fcntl.LOCK_UN)
    os.close(lock_fd)


def run_exchange_scenario(
    master_fd: int,
    pty_name: str,
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    scenario: str,
    iterations: int,
    reopen_count: int,
    transaction_count: int,
    controls: LoopbackControls,
):
    process = launch_process(
        exe_path,
        wineprefix,
        com_name,
        scenario,
        iterations,
        reopen_count,
        controls,
    )
    started_at = time.monotonic()
    for transaction_index in range(transaction_count):
        request, response = ORACLES[transaction_index % len(ORACLES)]
        observed = read_exact(master_fd, len(request), timeout=8.0 if transaction_index == 0 else 3.0)
        if observed != request:
            returncode, stdout, stderr, timed_out = finish_process(process, 2, scenario, com_name)
            print(
                f"unexpected request scenario={scenario} transaction={transaction_index + 1}/{transaction_count} "
                f"expected={request.hex(' ').upper()} observed={observed.hex(' ').upper()} "
                f"child-exit={returncode} timeout={str(timed_out).lower()} com={com_name}",
                file=sys.stderr,
            )
            print_process_output(stdout, stderr)
            return 3, 0
        try:
            write_all(master_fd, response)
        except OSError as exc:
            returncode, stdout, stderr, timed_out = finish_process(process, 2, scenario, com_name)
            print(
                f"response write failed scenario={scenario} transaction={transaction_index + 1}/{transaction_count} "
                f"child-exit={returncode} timeout={str(timed_out).lower()} com={com_name} error={exc}",
                file=sys.stderr,
            )
            print_process_output(stdout, stderr)
            return 3, 0

    child_timeout = max(10.0, 10.0 + transaction_count * 0.05 + controls.stale_wait_ms / 1000.0)
    returncode, stdout, stderr, timed_out = finish_process(process, child_timeout, scenario, com_name)
    print_process_output(stdout, stderr)
    if returncode != 0:
        print(
            f"serial child failed scenario={scenario} transaction={transaction_count}/{transaction_count} "
            f"child-exit={returncode} timeout={str(timed_out).lower()} com={com_name}",
            file=sys.stderr,
        )
        return returncode, 0

    trailing = read_buffered(master_fd)
    if trailing:
        print(
            f"unexpected trailing wire data scenario={scenario} transaction={transaction_count}/{transaction_count} "
            f"bytes={len(trailing)} child-exit={returncode} com={com_name}",
            file=sys.stderr,
        )
        return 3, 0

    elapsed_ms = int((time.monotonic() - started_at) * 1000)
    print(
        f"python serial scenario ok scenario={scenario} pty={pty_name} "
        f"reopen={reopen_count} iterations={iterations} transactions={transaction_count} "
        f"elapsed-ms={elapsed_ms} oracles={len(ORACLES)} fifo-verified=true"
    )
    return 0, transaction_count


def run_timeout_scenario(
    master_fd: int,
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    controls: LoopbackControls,
):
    scenario = "timeout"
    process = launch_process(
        exe_path,
        wineprefix,
        com_name,
        scenario,
        1,
        1,
        controls,
    )
    request = ORACLES[0][0]
    observed = read_exact(master_fd, len(request), timeout=8.0)
    if observed != request:
        returncode, stdout, stderr, timed_out = finish_process(process, 2, scenario, com_name)
        print(
            f"unexpected request scenario={scenario} transaction=1/1 expected={request.hex(' ').upper()} "
            f"observed={observed.hex(' ').upper()} child-exit={returncode} "
            f"timeout={str(timed_out).lower()} com={com_name}",
            file=sys.stderr,
        )
        print_process_output(stdout, stderr)
        return 3, 0

    returncode, stdout, stderr, timed_out = finish_process(
        process,
        timeout=max(5.0, (controls.timeout_ms + controls.close_wait_ms) / 1000.0 + 5.0),
        scenario=scenario,
        com_name=com_name,
    )
    print_process_output(stdout, stderr)
    if returncode != 0:
        print(
            f"serial child failed scenario={scenario} transaction=1/1 child-exit={returncode} "
            f"timeout={str(timed_out).lower()} com={com_name}",
            file=sys.stderr,
        )
        return returncode, 0
    trailing = read_buffered(master_fd)
    if trailing:
        print(
            f"unexpected trailing wire data scenario={scenario} transaction=1/1 "
            f"bytes={len(trailing)} child-exit={returncode} com={com_name}",
            file=sys.stderr,
        )
        return 3, 0
    print(
        f"python serial scenario ok scenario={scenario} request-observed=true response=withheld "
        f"timeout-ms={controls.timeout_ms} transactions=0"
    )
    return 0, 0


def run_interruption_scenario(
    master_fd: int,
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    scenario: str,
    controls: LoopbackControls,
):
    process = launch_process(
        exe_path,
        wineprefix,
        com_name,
        scenario,
        1,
        1,
        controls,
    )
    request_prefix = ORACLES[0][0]
    observed = read_exact(master_fd, len(request_prefix), timeout=8.0)
    if observed != request_prefix:
        returncode, stdout, stderr, timed_out = finish_process(process, 2, scenario, com_name)
        print(
            f"unexpected request prefix scenario={scenario} transaction=1/1 "
            f"expected={request_prefix.hex(' ').upper()} observed={observed.hex(' ').upper()} "
            f"child-exit={returncode} timeout={str(timed_out).lower()} com={com_name}",
            file=sys.stderr,
        )
        print_process_output(stdout, stderr)
        return 3, 0

    settlement_delay_ms = (
        controls.close_wait_ms
        if scenario == "close"
        else controls.cancel_wait_ms
    )
    time.sleep(settlement_delay_ms / 1000.0)
    wait_budget_ms = (
        controls.close_wait_ms
        if scenario == "close"
        else controls.cancel_wait_ms + controls.close_wait_ms
    )
    child_deadline = time.monotonic() + max(5.0, wait_budget_ms / 1000.0 + 5.0)
    wire_bytes = bytearray(observed)
    while process.poll() is None and time.monotonic() < child_deadline:
        wire_bytes.extend(read_buffered(master_fd, quiet_timeout=0.01))
        if len(wire_bytes) > MAX_CAPTURE_BYTES:
            raise RuntimeError(f"captured wire bytes exceed bound={MAX_CAPTURE_BYTES}")
        time.sleep(0.005)

    returncode, stdout, stderr, timed_out = finish_process(
        process,
        timeout=max(0.1, child_deadline - time.monotonic()),
        scenario=scenario,
        com_name=com_name,
    )
    print_process_output(stdout, stderr)
    if returncode != 0:
        print(
            f"serial child failed scenario={scenario} transaction=1/1 child-exit={returncode} "
            f"timeout={str(timed_out).lower()} com={com_name}",
            file=sys.stderr,
        )
        return returncode, 0

    wire_bytes.extend(read_buffered(master_fd))
    if scenario == "cancel" and CANCEL_PENDING_MARKER in wire_bytes:
        print(
            f"cancelled pending payload reached wire scenario={scenario} transaction=1/1 "
            f"child-exit={returncode} com={com_name} marker={CANCEL_PENDING_MARKER.hex(' ').upper()}",
            file=sys.stderr,
        )
        return 3, 0

    marker_state = (
        " pending-marker-on-wire=false cancellation-scope=pending-write-only "
        "active-write-cancellation-proven=false"
        if scenario == "cancel"
        else ""
    )
    print(
        f"python serial scenario ok scenario={scenario} request-prefix-observed=true response=withheld "
        f"settlement-drain=true wire-bytes={len(wire_bytes)} transactions=0{marker_state}"
    )
    return 0, 0


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    build_dir = Path(os.environ.get("SVM_MINGW_BUILD_DIR", repo_root / "build-windows-native-mingw"))
    exe_path = Path(os.environ.get("SVM_SERIAL_LOOPBACK_EXE", build_dir / "native_win32_serial_loopback_tests.exe"))
    wineprefix = Path(
        os.environ.get("SVM_WINEPREFIX") or os.environ.get("WINEPREFIX") or "/tmp/svm-native-serial-loopback-wine"
    )
    summary_path = os.environ.get("SVM_SERIAL_LOOPBACK_SUMMARY")
    if not invalidate_serial_summary(summary_path):
        return 2

    com_name = canonical_com_name(os.environ.get("SVM_SERIAL_LOOPBACK_COM", "COM5"))
    trace = os.environ.get("SVM_SERIAL_LOOPBACK_TRACE") == "1"
    scenarios = scenario_list_env("SVM_SERIAL_LOOPBACK_SCENARIOS", DEFAULT_SCENARIOS)
    iterations = positive_int_env("SVM_SERIAL_LOOPBACK_ITERATIONS", 1, 100000)
    reopen_count = positive_int_env("SVM_SERIAL_LOOPBACK_REOPEN_COUNT", 1, 10000)
    stress_iterations = positive_int_env("SVM_SERIAL_LOOPBACK_STRESS_ITERATIONS", 5000, 100000)
    stress_reopen_count = positive_int_env("SVM_SERIAL_LOOPBACK_STRESS_REOPEN_COUNT", 1, 10000)
    timeout_ms = positive_int_env("SVM_SERIAL_LOOPBACK_TIMEOUT_MS", 100, 60000)
    cancel_wait_ms = positive_int_env("SVM_SERIAL_LOOPBACK_CANCEL_WAIT_MS", 250, MAX_FAULT_WAIT_MS)
    close_wait_ms = positive_int_env("SVM_SERIAL_LOOPBACK_CLOSE_WAIT_MS", 250, MAX_FAULT_WAIT_MS)
    stale_wait_ms = positive_int_env("SVM_SERIAL_LOOPBACK_STALE_WAIT_MS", 100, 60000)
    wineboot_timeout = positive_int_env("SVM_SERIAL_LOOPBACK_WINEBOOT_TIMEOUT", 20, 300)
    if any(
        value is None
        for value in (
            com_name,
            scenarios,
            iterations,
            reopen_count,
            stress_iterations,
            stress_reopen_count,
            timeout_ms,
            cancel_wait_ms,
            close_wait_ms,
            stale_wait_ms,
            wineboot_timeout,
        )
    ):
        return 2

    controls = LoopbackControls(
        timeout_ms=timeout_ms,
        cancel_wait_ms=cancel_wait_ms,
        close_wait_ms=close_wait_ms,
        stale_wait_ms=stale_wait_ms,
        trace=trace,
    )

    if not exe_path.is_file():
        print(
            f"missing loopback test exe {SETUP_FAILURE_CONTEXT} path={exe_path} com={com_name}",
            file=sys.stderr,
        )
        return 2

    lock_fd = None
    try:
        wineprefix.mkdir(parents=True, exist_ok=True)
        lock_fd = acquire_matrix_lock(wineprefix, com_name)
        initialized = initialize_wineprefix(wineprefix, wineboot_timeout, com_name)
    except (OSError, RuntimeError) as exc:
        if lock_fd is not None:
            release_matrix_lock(lock_fd)
        print(
            f"wine setup failed scenario=setup transaction=n/a child-exit=not-started "
            f"com={com_name} error={exc}",
            file=sys.stderr,
        )
        return 2
    if not initialized:
        release_matrix_lock(lock_fd)
        return 2

    print(LOCAL_ONLY_GATE_NOTICE)
    print(
        "python serial matrix scenarios="
        f"{','.join(scenarios)} exe={exe_path} wineprefix={wineprefix} com={com_name}"
    )

    current_scenario = "setup"
    total_transactions = 0
    slave_paths = []
    try:
        dosdevices = wineprefix / "dosdevices"
        dosdevices.mkdir(parents=True, exist_ok=True)
        link_path = dosdevices / com_name.lower()
        for scenario in scenarios:
            current_scenario = scenario
            master_fd = None
            slave_fd = None
            try:
                master_fd, slave_fd = os.openpty()
                slave_path = os.ttyname(slave_fd)
                slave_paths.append(slave_path)
                tty.setraw(slave_fd, termios.TCSANOW)
                with temporary_com_mapping(link_path, slave_path):
                    os.close(slave_fd)
                    slave_fd = None

                    if scenario == "timeout":
                        returncode, transactions = run_timeout_scenario(
                            master_fd,
                            exe_path,
                            wineprefix,
                            com_name,
                            controls,
                        )
                    elif scenario in {"cancel", "close"}:
                        returncode, transactions = run_interruption_scenario(
                            master_fd,
                            exe_path,
                            wineprefix,
                            com_name,
                            scenario,
                            controls,
                        )
                    else:
                        if scenario == "stress":
                            scenario_iterations = stress_iterations
                            scenario_reopen_count = stress_reopen_count
                            transaction_count = stress_iterations * stress_reopen_count
                        elif scenario == "stale":
                            scenario_iterations = 1
                            scenario_reopen_count = 1
                            transaction_count = 2
                        else:
                            scenario_iterations = iterations
                            if scenario == "reopen" and not env_has_value("SVM_SERIAL_LOOPBACK_REOPEN_COUNT"):
                                scenario_reopen_count = 3
                            else:
                                scenario_reopen_count = reopen_count
                            transaction_count = scenario_iterations * scenario_reopen_count
                        returncode, transactions = run_exchange_scenario(
                            master_fd,
                            slave_path,
                            exe_path,
                            wineprefix,
                            com_name,
                            scenario,
                            scenario_iterations,
                            scenario_reopen_count,
                            transaction_count,
                            controls,
                        )
                    if returncode != 0:
                        return returncode
                    total_transactions += transactions
            finally:
                if slave_fd is not None:
                    os.close(slave_fd)
                if master_fd is not None:
                    os.close(master_fd)
    except (OSError, RuntimeError) as exc:
        print(
            f"serial PTY harness failed scenario={current_scenario} transaction=n/a "
            f"child-exit=unavailable com={com_name} error={exc}",
            file=sys.stderr,
        )
        return 2
    finally:
        terminate_active_processes()
        release_matrix_lock(lock_fd)
    scenario_text = ",".join(scenarios)
    pty_text = ",".join(dict.fromkeys(slave_paths))
    try:
        write_serial_summary(
            summary_path,
            [
                "Native serial PTY matrix summary",
                "GateStatus=passed",
                "Classification=local-only-release-candidate-evidence",
                f"Scenarios={scenario_text}",
                f"Transactions={total_transactions}",
                "TransactionsMeaning=completed-request-response-exchanges",
                f"ComPort={com_name}",
                f"Pty={pty_text}",
                f"PtyInstanceCount={len(slave_paths)}",
                f"UniquePtyCount={len(dict.fromkeys(slave_paths))}",
                "PtyIsolation=per-scenario",
                f"Executable={exe_path}",
                "Transport=serial-adapter-contract",
                f"WinePrefix={wineprefix}",
                "CiExecutesPtyMatrix=no",
            ],
        )
    except OSError as exc:
        print(
            f"serial summary write failed scenario=summary transaction=n/a child-exit=n/a "
            f"path={summary_path} com={com_name} error={exc}",
            file=sys.stderr,
        )
        return 2

    print(
        f"python serial matrix ok pty-count={len(slave_paths)} com={com_name} "
        f"scenarios={scenario_text} transactions={total_transactions}"
    )
    print(
        "python serial matrix summary "
        f"gate-status=passed classification=local-only-release-candidate-evidence "
        f"scenarios={scenario_text} transactions={total_transactions} ci-executes-pty-matrix=no"
    )
    return 0


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, exit_on_signal)
    signal.signal(signal.SIGHUP, exit_on_signal)
    try:
        raise SystemExit(main())
    finally:
        terminate_active_processes()
