# Phase 1: Contract Foundation

> Parent: [Project Plan](../../project-plan.md)
> Status: Pending

---

## Objective

Define the smallest neutral session, capability, operation-result, and bounded-queue contracts before changing the Win32 backend.

## Prerequisites

- [ ] The approved project plan remains the source of truth.
- [ ] Current host contract, queue, command-sequence, and RTU adapter tests pass before changes.

## Libraries & Dependencies

No external libraries. This phase uses only C++20 standard-library facilities and existing project modules.

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Define session model | Add neutral state, generation, operation, error, result, snapshot, and capability contracts in one focused session header. | `src/transport/serial_session.h`; `src/transport/serial_types.h`; `tests/transport_contract_tests.cpp` | 9 |
| 2 | Migrate command write capability | Replace the queue-owned write-port abstraction with the session write capability while preserving command admission semantics and metadata. | `src/command_sequence/command_sequence.h`; `src/command_sequence/command_sequence.cpp`; `tests/command_sequence_tests.cpp` | 8 |
| 3 | Enforce dual-budget queue | Add 64-request and 256 KiB accounting, explicit active work, byte watermarks, deadlines, and deterministic release. | `src/transport/serial_write_queue.h`; `src/transport/serial_write_queue.cpp`; `tests/serial_write_queue_tests.cpp`; `tests/native_file_send_state_tests.cpp` | 10 |
| 4 | Build fake session contract suite | Replace the broad fake with deterministic session/capability tests for state, generation, typed results, deadline, cancel, close, and stale completion. | `tests/transport_contract_tests.cpp`; `CMakeLists.txt` | 8 |

## Deliverables

- [ ] One neutral `serial_session.h` containing only meaningful session/capability contracts.
- [ ] Structured session and operation result types with generation and native evidence fields.
- [ ] A queue whose count and byte budgets are fully tested.
- [ ] Command writes compiled against the narrow scheduler capability and a complete fake session contract suite.

## Verification Checklist

- [ ] Run focused host tests for transport contracts, queue, command sequence, and file send.
- [ ] Run a fresh host build with `SVM_BUILD_WIN32_APP=OFF` and all host CTest targets.
- [ ] Confirm no neutral transport header includes Win32 or UI headers.
- [ ] Confirm queue rejection never removes older work and IDs remain monotonic.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| Too many small interface files recreate abstraction clutter. | Keep lifecycle, byte, and write capability declarations together in `serial_session.h`; add no generic backend framework. |
| Command sequence currently treats queue acceptance as step completion. | Preserve that behavior explicitly and add generation/category metadata without changing execution semantics. |
| Fake tests accidentally define behavior the Win32 backend cannot support. | Keep the contract aligned with approved synchronous deadlines and verify every state transition again in Phase 2 native tests. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
