# Phase 2: Backend Consistency

> Parent: [Project Plan](../../project-plan.md)
> Status: completed

---

## Objective

Unify serial write, Modbus transaction, and native storage behavior behind deterministic, testable, non-blocking paths.

## Prerequisites

- [x] Phase 1 completed and UI/layout/controller seams are stable.
- [x] UI perf and screenshot baseline is available.
- [x] No TCP runtime or SQLite backend is introduced in this phase.

## Libraries & Dependencies

| Library | GitHub Repo | Used For |
|---------|-------------|----------|
| Win32 communications | microsoft/Windows-classic-samples | Native serial handle and background I/O reference patterns. |
| Qt SerialPort | qt/qtserialport | Behavioral reference only for serial timeouts/errors; no native release runtime dependency. |
| CMake | Kitware/CMake | CTest target structure and native package gate integration. |

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Define Serial Write Queue Contract | Add bounded write request/result model with accepted/sent/failed/timeout/cancelled states. | `src/transport/serial_write_queue.h`, `src/transport/serial_write_queue.cpp`, `src/transport/serial_port_service.h`, `src/transport/serial_port_service.cpp`, `src/win32/native_serial_io_state.h`, `src/win32/native_serial_io_state.cpp`, `tests/serial_write_queue_tests.cpp`, `tests/native_serial_io_state_tests.cpp` | 11 |
| 2 | Integrate Async Write Queue | Route manual send and file/batch send through non-blocking queue behavior. | `src/win32/main_window_serial_io.cpp`, `src/win32/main_window_send.cpp`, `src/win32/win32_serial_port.h`, `src/win32/win32_serial_port.cpp`, `src/transport/serial_write_queue.h`, `src/transport/serial_write_queue.cpp`, `tests/native_win32_serial_tests.cpp`, `tests/native_file_send_state_tests.cpp`, `tests/serial_write_queue_tests.cpp` | 14 |
| 3 | Clarify Fake and PTY Stress Gates | Separate CI-blocking fake/native tests from current local-only PTY loopback evidence. | `scripts/run-windows-native-serial-pty-loopback.py`, `.github/workflows/windows-native-package.yml`, `docs/Windows串口真机验收.md`, `docs/测试与验证.md` | 9 |
| 4 | Unify Native Modbus Worker Adapter | Make native worker delegate protocol behavior to `modbus_scan_executor` and transport abstractions. | `src/win32/native_modbus_scan_worker.h`, `src/win32/native_modbus_scan_worker.cpp`, `src/modbus/modbus_scan_executor.h`, `src/modbus/modbus_scan_executor.cpp`, `src/modbus/modbus_rtu_serial_transport.h`, `src/modbus/modbus_rtu_serial_transport.cpp`, `tests/modbus_scan_executor_tests.cpp`, `tests/modbus_rtu_serial_transport_tests.cpp`, `tests/native_modbus_scan_request_tests.cpp` | 13 |
| 5 | Normalize Modbus Result Semantics | Lock timeout, retry, exception, CRC/length, and data-format behavior in one result model. | `src/modbus/modbus_read_response.h`, `src/modbus/modbus_read_response.cpp`, `src/modbus/modbus_scan_plan.h`, `src/modbus/modbus_scan_plan.cpp`, `src/win32/native_modbus_scan_ui_state.h`, `src/win32/native_modbus_scan_ui_state.cpp`, `tests/modbus_read_response_tests.cpp`, `tests/modbus_scan_plan_tests.cpp`, `tests/native_modbus_scan_ui_state_tests.cpp` | 12 |
| 6 | Narrow Native Session Store Boundary | Split storage interface expectations from native file backend behavior without introducing SQLite. | `src/storage/session_store_port.h`, `src/native_storage/native_session_store.h`, `src/native_storage/native_store_files.h`, `src/native_storage/native_store_files.cpp`, `src/storage/session_store.h`, `tests/native_storage_tests.cpp`, `tests/session_store_tests.cpp` | 14 |
| 7 | Add File Commit and Orphan Recovery | Add schema/commit/recovery discipline for native file records and recovery scans. | `src/native_storage/native_session_store.h`, `src/native_storage/native_session_store.cpp`, `src/native_storage/native_store_file_ops.h`, `src/native_storage/native_store_file_ops.cpp`, `src/native_storage/native_store_record_io.h`, `src/native_storage/native_store_record_io.cpp`, `tests/native_storage_tests.cpp` | 14 |
| 8 | Phase 2 Backend Regression Closure | Establish backend consistency gate covering serial queue, Modbus executor, storage recovery, and stress tests. | `tests/quality_regression_tests.cpp`, `tests/quality_stress_tests.cpp`, `.github/workflows/windows-native-package.yml`, `docs/测试与验证.md` | 9 |

## Deliverables

- [x] Manual send and future batch/command sends share non-blocking bounded write semantics.
- [x] Native Modbus UI/worker behavior delegates to one executor/transaction model.
- [x] Native file storage supports schema/commit/recovery behavior with tests.
- [x] PTY loopback is correctly classified as local pre-release evidence unless CI support is added.

## Verification Checklist

- [x] Local CTest passes.
- [x] Native package workflow/local package audit passes existing blocking gates.
- [x] Serial fake/native tests cover timeout/cancel/failure/backpressure.
- [x] Modbus executor tests cover normal, exception, malformed, timeout, and retry behavior.
- [x] Storage tests cover schema/header, partial write, orphan recovery, and existing data compatibility.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| Async write queue breaks manual-send confidence | Emit and surface explicit accepted/sent/failed/timeout/cancelled events. |
| Modbus worker unification regresses scanning | Lock tests before swapping UI path; keep adapter small. |
| Storage recovery corrupts existing sessions | Add compatibility fixtures and recovery tests before enabling new writes. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
