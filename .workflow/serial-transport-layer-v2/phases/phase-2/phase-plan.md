# Phase 2: Win32 Session and Caller Migration

> Parent: [Project Plan](../../project-plan.md)
> Status: Pending

---

## Objective

Create the sole Win32 HANDLE owner, migrate every in-repo caller, and remove the broad transport facade without a forwarding compatibility layer.

## Prerequisites

- [ ] Phase 1 contracts and focused tests are complete.
- [ ] The current Win32 serial, reconnect, UI-state, command, and RTU tests pass under MinGW/Windows.

## Libraries & Dependencies

| Library | GitHub Repo | Used For |
|---------|-------------|----------|
| Windows API (platform) | `microsoft/Windows-classic-samples` | COM HANDLE lifecycle, DCB/COMMTIMEOUTS, synchronous I/O, events, cancellation, and control lines. |

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Harden Win32 session owner | Rename the concrete backend and place handle/configuration/control ownership in one move-disabled session object while keeping the build green. | Create `src/win32/win32_serial_session.h`, `src/win32/win32_serial_session.cpp`; delete `src/win32/win32_serial_port.h`, `src/win32/win32_serial_port.cpp`; modify `CMakeLists.txt`, `src/win32/main_window.h`, `tests/native_win32_serial_tests.cpp` | 12 |
| 2 | Implement generation and settlement | Add explicit session states, generation invalidation/publication, deadlines, pending/active settlement, and typed native error mapping. | `src/win32/win32_serial_session.h`; `src/win32/win32_serial_session.cpp`; `tests/native_win32_serial_tests.cpp`; `tests/native_reconnect_state_tests.cpp` | 12 |
| 3 | Migrate main-window lifecycle | Move connect, disconnect, state snapshots, endpoint, DTR/RTS, reconnect decisions, and persisted endpoint access to typed session operations. | `src/win32/main_window.h`; `src/win32/main_window_serial.cpp`; `src/win32/main_window_commands.cpp`; `src/win32/main_window_storage.cpp` | 10 |
| 4 | Migrate main-window I/O | Move polling, manual/file enqueue, pending-result matching, cancellation, and queue display to generation-aware typed session results. | `src/win32/main_window.h`; `src/win32/main_window_serial_io.cpp`; `src/win32/main_window_send.cpp`; `src/win32/native_serial_io_state.h`; `src/win32/native_serial_io_state.cpp`; `tests/native_serial_io_state_tests.cpp` | 12 |
| 5 | Migrate RTU and Modbus borrowing | Make RTU depend only on the byte capability, give the worker a captured generation, and prevent stale completion from updating a replacement session. | `src/transport/serial_rtu_transport.h`; `src/transport/serial_rtu_transport.cpp`; `src/win32/native_modbus_scan_worker.h`; `src/win32/native_modbus_scan_worker.cpp`; `src/win32/main_window_modbus.cpp`; `tests/native_modbus_transport_adapter_tests.cpp`; `tests/native_protocol_modbus_tests.cpp` | 12 |
| 6 | Remove broad transport facade | Remove `SerialTransport`, drop queue-owned capability inheritance and localized-error decision paths, then update all remaining includes and contract/native tests. | Delete `src/transport/serial_transport.h`; modify `src/transport/serial_session.h`, `src/transport/serial_write_queue.h`, `src/win32/win32_serial_session.h`, `src/win32/win32_serial_session.cpp`, `tests/transport_contract_tests.cpp`, `tests/native_win32_serial_tests.cpp`, `CMakeLists.txt` | 9 |

## Deliverables

- [ ] `Win32SerialSession` is the only concrete owner of the active COM HANDLE.
- [ ] Main-window, RTU, and Modbus paths use session/capability contracts and typed results.
- [ ] Pending UI writes are matched by `(generation, requestId)`.
- [ ] The broad facade, old concrete port files, and all forwarding aliases are absent.

## Verification Checklist

- [ ] Build all MinGW CTest targets, not only `svm-native-win32`.
- [ ] Run native serial, reconnect, UI-state, command, RTU adapter, and Modbus tests.
- [ ] Run `rg` checks proving `SerialTransport`, `Win32SerialPort`, and `lastErrorText` transport decisions are gone.
- [ ] Confirm UI ownership policy remains separate from native HANDLE ownership.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| Renaming the backend and migrating callers creates a large but necessary diff. | Keep backend, main-window, Modbus, and final facade removal as separate verified tasks. |
| The UI timer and Modbus worker race the same session. | Preserve current UI arbitration, capture generation for the worker, and require close/join ordering. |
| Synchronous cancellation is overstated. | Use explicit deadlines/native cancellation and tests; preserve the future overlapped backend seam. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
