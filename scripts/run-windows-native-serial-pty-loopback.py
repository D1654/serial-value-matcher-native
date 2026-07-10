#!/usr/bin/env python3
import errno
import os
import select
import signal
import subprocess
import sys
import termios
import time
import tty
from pathlib import Path


REQUEST = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B])
RESPONSE = bytes([0x01, 0x03, 0x04, 0x41, 0x48, 0x00, 0x00, 0x7B, 0xF3])
VALID_SCENARIOS = {"normal", "reopen", "timeout", "cancel", "stress"}
LOCAL_ONLY_GATE_NOTICE = (
    "python serial matrix gate classification: local-only release-candidate evidence; "
    "Windows GitHub Actions package workflow records this requirement but does not execute POSIX PTY scenarios"
)


def positive_int_env(name: str, default: int, maximum: int):
    value = os.environ.get(name)
    if value is None or value == "":
        return default
    try:
        parsed = int(value, 10)
    except ValueError:
        print(f"{name} must be an integer in [1, {maximum}], got: {value}", file=sys.stderr)
        return None
    if parsed < 1 or parsed > maximum:
        print(f"{name} must be an integer in [1, {maximum}], got: {value}", file=sys.stderr)
        return None
    return parsed


def env_has_value(name: str) -> bool:
    return os.environ.get(name) not in (None, "")


def scenario_list_env(name: str, default: str):
    value = os.environ.get(name, default)
    scenarios = [part.strip().lower() for part in value.split(",") if part.strip()]
    if not scenarios:
        print(f"{name} must contain at least one scenario", file=sys.stderr)
        return None
    invalid = [scenario for scenario in scenarios if scenario not in VALID_SCENARIOS]
    if invalid:
        print(
            f"{name} has invalid scenario(s): {', '.join(invalid)}; "
            f"valid={','.join(sorted(VALID_SCENARIOS))}",
            file=sys.stderr,
        )
        return None
    return scenarios


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


def drain_fd(fd: int) -> None:
    while True:
        readable, _, _ = select.select([fd], [], [], 0)
        if not readable:
            return
        try:
            if not os.read(fd, 4096):
                return
        except OSError as exc:
            if exc.errno == errno.EIO:
                return
            raise


def print_process_output(stdout: bytes, stderr: bytes) -> None:
    if stdout:
        print(stdout.decode(errors="replace"), end="")
    if stderr:
        print(stderr.decode(errors="replace"), end="", file=sys.stderr)


def write_serial_summary(path: str | None, lines: list[str]) -> None:
    if not path:
        return
    summary_path = Path(path)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def finish_process(process: subprocess.Popen, timeout: float, scenario: str):
    try:
        stdout, stderr = process.communicate(timeout=timeout)
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
        print(f"scenario {scenario} timed out and was killed", file=sys.stderr)
        return 124, stdout, stderr, True


def launch_process(
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    scenario: str,
    iterations: int,
    reopen_count: int,
    timeout_ms: int,
    cancel_wait_ms: int,
    trace: bool,
):
    env = os.environ.copy()
    env["WINEPREFIX"] = str(wineprefix)
    env["SVM_NATIVE_SERIAL_LOOPBACK_PORT"] = com_name
    env["SVM_NATIVE_SERIAL_LOOPBACK_SCENARIO"] = scenario
    env["SVM_NATIVE_SERIAL_LOOPBACK_ITERATIONS"] = str(iterations)
    env["SVM_NATIVE_SERIAL_LOOPBACK_REOPEN_COUNT"] = str(reopen_count)
    env["SVM_NATIVE_SERIAL_LOOPBACK_TIMEOUT_MS"] = str(timeout_ms)
    env["SVM_NATIVE_SERIAL_LOOPBACK_CANCEL_WAIT_MS"] = str(cancel_wait_ms)
    if trace:
        env["SVM_NATIVE_SERIAL_LOOPBACK_TRACE"] = "1"
    return subprocess.Popen(
        ["wine", str(exe_path)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=False,
        start_new_session=True,
    )


def initialize_wineprefix(wineprefix: Path, timeout_seconds: int) -> bool:
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
    returncode, stdout, stderr, _ = finish_process(process, timeout_seconds, "wineboot")
    if returncode != 0:
        print(f"wineboot failed for prefix: {wineprefix}", file=sys.stderr)
        print_process_output(stdout, stderr)
        return False
    return True


def run_exchange_scenario(
    master_fd: int,
    pty_name: str,
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    scenario: str,
    iterations: int,
    reopen_count: int,
    timeout_ms: int,
    cancel_wait_ms: int,
    trace: bool,
):
    total_transactions = iterations * reopen_count
    process = launch_process(
        exe_path,
        wineprefix,
        com_name,
        scenario,
        iterations,
        reopen_count,
        timeout_ms,
        cancel_wait_ms,
        trace,
    )
    started_at = time.monotonic()
    for transaction_index in range(total_transactions):
        request = read_exact(master_fd, len(REQUEST), timeout=8.0 if transaction_index == 0 else 3.0)
        if request != REQUEST:
            returncode, stdout, stderr, _ = finish_process(process, 2, scenario)
            print(
                f"unexpected request scenario={scenario} transaction={transaction_index + 1}/{total_transactions}: "
                f"{request.hex(' ').upper()} returncode={returncode}",
                file=sys.stderr,
            )
            print_process_output(stdout, stderr)
            return 3, 0
        os.write(master_fd, RESPONSE)

    returncode, stdout, stderr, _ = finish_process(
        process,
        timeout=max(10.0, 10.0 + total_transactions * 0.05),
        scenario=scenario,
    )
    print_process_output(stdout, stderr)
    if returncode != 0:
        return returncode, 0

    elapsed_ms = int((time.monotonic() - started_at) * 1000)
    print(
        f"python serial scenario ok scenario={scenario} pty={pty_name} "
        f"reopen={reopen_count} iterations={iterations} transactions={total_transactions} "
        f"elapsed-ms={elapsed_ms} request={REQUEST.hex(' ').upper()} response={RESPONSE.hex(' ').upper()}"
    )
    return 0, total_transactions


def run_timeout_scenario(
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    timeout_ms: int,
    cancel_wait_ms: int,
    trace: bool,
):
    process = launch_process(exe_path, wineprefix, com_name, "timeout", 1, 1, timeout_ms, cancel_wait_ms, trace)
    returncode, stdout, stderr, _ = finish_process(
        process,
        timeout=max(5.0, timeout_ms / 1000.0 + 5.0),
        scenario="timeout",
    )
    print_process_output(stdout, stderr)
    if returncode != 0:
        return returncode, 0
    print(f"python serial scenario ok scenario=timeout timeout-ms={timeout_ms} transactions=0")
    return 0, 0


def run_cancel_scenario(
    master_fd: int,
    exe_path: Path,
    wineprefix: Path,
    com_name: str,
    timeout_ms: int,
    cancel_wait_ms: int,
    trace: bool,
):
    process = launch_process(exe_path, wineprefix, com_name, "cancel", 1, 1, timeout_ms, cancel_wait_ms, trace)
    request = read_exact(master_fd, len(REQUEST), timeout=8.0)
    if request != REQUEST:
        returncode, stdout, stderr, _ = finish_process(process, 2, "cancel")
        print(
            f"unexpected cancel request: {request.hex(' ').upper()} returncode={returncode}",
            file=sys.stderr,
        )
        print_process_output(stdout, stderr)
        return 3, 0

    returncode, stdout, stderr, _ = finish_process(
        process,
        timeout=max(5.0, cancel_wait_ms / 1000.0 + 5.0),
        scenario="cancel",
    )
    print_process_output(stdout, stderr)
    if returncode != 0:
        return returncode, 0
    print(
        f"python serial scenario ok scenario=cancel request-observed=true response=withheld "
        f"cancel-wait-ms={cancel_wait_ms} transactions=1"
    )
    return 0, 1


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    build_dir = Path(os.environ.get("SVM_MINGW_BUILD_DIR", repo_root / "build-windows-native-mingw"))
    exe_path = Path(os.environ.get("SVM_SERIAL_LOOPBACK_EXE", build_dir / "native_win32_serial_loopback_tests.exe"))
    wineprefix = Path(
        os.environ.get("SVM_WINEPREFIX") or os.environ.get("WINEPREFIX") or "/tmp/svm-native-serial-loopback-wine"
    )
    com_name = os.environ.get("SVM_SERIAL_LOOPBACK_COM", "COM5")
    trace = os.environ.get("SVM_SERIAL_LOOPBACK_TRACE") == "1"
    summary_path = os.environ.get("SVM_SERIAL_LOOPBACK_SUMMARY")
    scenarios = scenario_list_env("SVM_SERIAL_LOOPBACK_SCENARIOS", "normal,reopen,timeout,cancel")
    iterations = positive_int_env("SVM_SERIAL_LOOPBACK_ITERATIONS", 1, 100000)
    reopen_count = positive_int_env("SVM_SERIAL_LOOPBACK_REOPEN_COUNT", 1, 10000)
    stress_iterations = positive_int_env("SVM_SERIAL_LOOPBACK_STRESS_ITERATIONS", 5000, 100000)
    stress_reopen_count = positive_int_env("SVM_SERIAL_LOOPBACK_STRESS_REOPEN_COUNT", 1, 10000)
    timeout_ms = positive_int_env("SVM_SERIAL_LOOPBACK_TIMEOUT_MS", 100, 60000)
    cancel_wait_ms = positive_int_env("SVM_SERIAL_LOOPBACK_CANCEL_WAIT_MS", 150, 60000)
    wineboot_timeout = positive_int_env("SVM_SERIAL_LOOPBACK_WINEBOOT_TIMEOUT", 20, 300)
    if (
        scenarios is None
        or iterations is None
        or reopen_count is None
        or stress_iterations is None
        or stress_reopen_count is None
        or timeout_ms is None
        or cancel_wait_ms is None
        or wineboot_timeout is None
    ):
        return 2

    if not exe_path.is_file():
        print(f"missing loopback test exe: {exe_path}", file=sys.stderr)
        return 2

    if not initialize_wineprefix(wineprefix, wineboot_timeout):
        return 2

    print(LOCAL_ONLY_GATE_NOTICE)
    print(
        "python serial matrix scenarios="
        f"{','.join(scenarios)} exe={exe_path} wineprefix={wineprefix} com={com_name}"
    )

    master_fd, slave_fd = os.openpty()
    slave_path = os.ttyname(slave_fd)
    tty.setraw(slave_fd, termios.TCSANOW)

    dosdevices = wineprefix / "dosdevices"
    dosdevices.mkdir(parents=True, exist_ok=True)
    link_path = dosdevices / com_name.lower()
    if link_path.exists() or link_path.is_symlink():
        link_path.unlink()
    link_path.symlink_to(slave_path)
    os.close(slave_fd)

    total_transactions = 0
    try:
        for scenario in scenarios:
            drain_fd(master_fd)
            if scenario == "timeout":
                returncode, transactions = run_timeout_scenario(
                    exe_path, wineprefix, com_name, timeout_ms, cancel_wait_ms, trace
                )
            elif scenario == "cancel":
                returncode, transactions = run_cancel_scenario(
                    master_fd, exe_path, wineprefix, com_name, timeout_ms, cancel_wait_ms, trace
                )
            else:
                scenario_iterations = stress_iterations if scenario == "stress" else iterations
                if scenario == "stress":
                    scenario_reopen_count = stress_reopen_count
                elif scenario == "reopen" and not env_has_value("SVM_SERIAL_LOOPBACK_REOPEN_COUNT"):
                    scenario_reopen_count = 3
                else:
                    scenario_reopen_count = reopen_count
                returncode, transactions = run_exchange_scenario(
                    master_fd,
                    slave_path,
                    exe_path,
                    wineprefix,
                    com_name,
                    scenario,
                    scenario_iterations,
                    scenario_reopen_count,
                    timeout_ms,
                    cancel_wait_ms,
                    trace,
                )
            if returncode != 0:
                return returncode
            total_transactions += transactions
    finally:
        os.close(master_fd)

    scenario_text = ",".join(scenarios)
    print(f"python serial matrix ok pty={slave_path} com={com_name} scenarios={scenario_text} transactions={total_transactions}")
    print(
        "python serial matrix summary "
        f"gate-status=passed classification=local-only-release-candidate-evidence "
        f"scenarios={scenario_text} transactions={total_transactions}"
    )
    write_serial_summary(
        summary_path,
        [
            "Native serial PTY matrix summary",
            "GateStatus=passed",
            "Classification=local-only-release-candidate-evidence",
            f"Scenarios={scenario_text}",
            f"Transactions={total_transactions}",
            f"ComPort={com_name}",
            f"Pty={slave_path}",
            f"Executable={exe_path}",
            f"WinePrefix={wineprefix}",
        ],
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
