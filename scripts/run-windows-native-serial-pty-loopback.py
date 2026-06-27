#!/usr/bin/env python3
import errno
import os
import select
import subprocess
import sys
import termios
import time
import tty
from pathlib import Path


REQUEST = bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B])
RESPONSE = bytes([0x01, 0x03, 0x04, 0x41, 0x48, 0x00, 0x00, 0x7B, 0xF3])


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


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    build_dir = Path(os.environ.get("SVM_MINGW_BUILD_DIR", repo_root / "build-windows-native-mingw"))
    exe_path = Path(os.environ.get("SVM_SERIAL_LOOPBACK_EXE", build_dir / "native_win32_serial_loopback_tests.exe"))
    wineprefix = Path(os.environ.get("SVM_WINEPREFIX", "/tmp/svm-native-serial-loopback-wine"))
    com_name = os.environ.get("SVM_SERIAL_LOOPBACK_COM", "COM5")

    if not exe_path.is_file():
        print(f"missing loopback test exe: {exe_path}", file=sys.stderr)
        return 2

    master_fd, slave_fd = os.openpty()
    slave_path = os.ttyname(slave_fd)
    tty.setraw(slave_fd, termios.TCSANOW)

    dosdevices = wineprefix / "dosdevices"
    dosdevices.mkdir(parents=True, exist_ok=True)
    link_path = dosdevices / com_name.lower()
    if link_path.exists() or link_path.is_symlink():
        link_path.unlink()
    link_path.symlink_to(slave_path)

    env = os.environ.copy()
    env["WINEPREFIX"] = str(wineprefix)
    env["SVM_NATIVE_SERIAL_LOOPBACK_PORT"] = com_name
    process = subprocess.Popen(
        ["wine", str(exe_path)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        text=False,
    )
    os.close(slave_fd)

    request = read_exact(master_fd, len(REQUEST), timeout=8.0)
    if request != REQUEST:
        try:
            stdout, stderr = process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate(timeout=2)
        print(f"unexpected request: {request.hex(' ').upper()}", file=sys.stderr)
        print(stdout.decode(errors="replace"), end="")
        print(stderr.decode(errors="replace"), end="", file=sys.stderr)
        os.close(master_fd)
        return 3

    os.write(master_fd, RESPONSE)
    stdout, stderr = process.communicate(timeout=10)
    os.close(master_fd)
    print(stdout.decode(errors="replace"), end="")
    if stderr:
        print(stderr.decode(errors="replace"), end="", file=sys.stderr)
    if process.returncode != 0:
        return process.returncode

    print(f"python serial peer ok pty={slave_path} request={request.hex(' ').upper()} response={RESPONSE.hex(' ').upper()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
