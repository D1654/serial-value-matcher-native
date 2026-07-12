# Phase 3: Production Hardening

> Parent: [Project Plan](../../project-plan.md)
> Status: Pending

---

## Objective

Make the migrated serial session measurable and failure-safe under queue pressure, cancellation, disconnect, reconnect, PTY faults, and release packaging.

## Prerequisites

- [ ] Phase 2 migration is complete and the old facade is absent.
- [ ] Host and MinGW focused serial tests pass before hardening begins.

## Libraries & Dependencies

| Library | GitHub Repo | Used For |
|---------|-------------|----------|
| Windows API (platform) | `microsoft/Windows-classic-samples` | Synchronous I/O cancellation, events, errors, and COM lifecycle evidence. |
| CMake/CTest | `Kitware/CMake` | Test registration and host/cross-platform execution. |
| Wine | `wine-mirror/wine` | Local Windows executable, self-test, UI performance, and PTY evidence on Linux. |
| GitHub Actions | `actions/runner` | Windows build/test/package and artifact evidence. |

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Integrate queue backpressure | Connect dual count/byte accounting and active-request identity to the real session and UI queue snapshot. | `src/win32/win32_serial_session.h`; `src/win32/win32_serial_session.cpp`; `src/win32/native_serial_io_state.h`; `src/win32/native_serial_io_state.cpp`; `src/win32/main_window_serial_io.cpp`; `tests/native_serial_io_state_tests.cpp` | 10 |
| 2 | Guarantee exactly-once completion | Settle every accepted request once across sent, failed, timeout, cancel, disconnect, close, and reopen paths within the approved deterministic target. | `src/win32/win32_serial_session.h`; `src/win32/win32_serial_session.cpp`; `tests/native_win32_serial_tests.cpp`; `tests/native_reconnect_state_tests.cpp` | 12 |
| 3 | Wire typed errors and evidence | Map structured operation evidence to UI text/log records without transport text parsing or default payload logging. | `src/transport/serial_types.h`; `src/win32/win32_serial_session.cpp`; `src/win32/main_window_serial.cpp`; `src/win32/main_window_serial_io.cpp`; `src/win32/main_window_log.cpp`; `src/win32/native_log_model.h`; `tests/transport_contract_tests.cpp` | 9 |
| 4 | Expand PTY fault matrix | Extend loopback coverage for cancel/close/reopen, stale generations, timeout, stress, and no-port safety, retaining release-candidate summary semantics. | `tests/native_win32_serial_loopback_tests.cpp`; `tests/native_win32_serial_tests.cpp`; `scripts/run-windows-native-serial-pty-loopback.py` | 12 |
| 5 | Strengthen CI release gates | Register new sources/tests and make Windows workflows/package/docs checks require the transport-v2 evidence without weakening existing size or runtime gates. | `CMakeLists.txt`; `.github/workflows/windows-native-package.yml`; `.github/workflows/windows-native-ui-capture.yml`; `scripts/package-windows-native-mingw.sh`; `scripts/check-docs-artifact-consistency.py` | 10 |

## Deliverables

- [ ] Deterministic count/byte backpressure and UI-visible queue snapshots.
- [ ] Exactly-once terminal results for all accepted operations.
- [ ] Structured local evidence with payload-safe defaults.
- [ ] PTY fault/stress matrix and CI/package enforcement.

## Verification Checklist

- [ ] Run focused host and MinGW tests before the full CTest suites.
- [ ] Run PTY scenarios `normal,reopen,timeout,cancel,stress` and require `gate-status=passed`.
- [ ] Run Wine self-test and UI performance checks in strict mode.
- [ ] Inspect package size, imports, forbidden runtimes, required files, and SHA256.
- [ ] Run documentation consistency and `git diff --check`.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| Cancellation tests pass on PTY but not a real driver. | Label PTY evidence correctly and retain optional hardware smoke guidance. |
| Added evidence exposes payload bytes. | Keep transport events metadata-only; existing explicit raw evidence remains a separate user-visible feature. |
| MinGW script builds only the app target. | Build the entire cross CMake tree before invoking cross CTest. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
