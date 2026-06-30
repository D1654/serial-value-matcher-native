# API Reference - Phase 4 Task 04: Serial PTY Edge Matrix

Generated: 2026-06-30T21:00:05+08:00

## Sources

| API Area | Source | Confidence |
|---|---|---|
| Win32 serial examples | DeepWiki query against `microsoft/Windows-classic-samples` | Medium |
| Local Win32 serial port wrapper | `src/win32/win32_serial_port.cpp` | High |
| Local PTY loopback harness | `scripts/run-windows-native-serial-pty-loopback.py` | High |

## Win32 Serial Notes

- DeepWiki found Windows classic samples that primarily demonstrate overlapped COM I/O with event waits and cancellation signals.
- The local Win32 native implementation intentionally uses synchronous `CreateFileW`, `SetCommTimeouts`, `ClearCommError`, `ReadFile`, and `WriteFile`.
- `Win32SerialPort::waitForReadyRead(0)` is a deterministic no-data timeout check because it returns false without setting `lastErrorText()` when the input queue is empty.
- Reopen coverage should close and recreate `Win32SerialPort` handles, preserving the current reopen stress behavior.

## PTY Harness Notes

- The PTY peer owns the POSIX master side and links Wine `dosdevices/comN` to the slave path.
- Normal and reopen scenarios should expect exact request bytes and return exact response bytes.
- Timeout scenarios should intentionally withhold a response and require the test executable to exit without a peer write.
- Cancel scenarios can be driven by the harness by terminating the Wine test process after observing the first request; the gate is deterministic only if the process exits within a bounded timeout and does not leave the peer loop blocked.
