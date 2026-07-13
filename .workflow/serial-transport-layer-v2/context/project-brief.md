# Project Brief: Serial Transport Layer v2

## User Request

Use the Workflow Architect process to plan the next optimization of the unified transport layer.

## Established Baseline

- The legacy Qt route has been removed.
- The application is Win32 native and uses C++20, CMake, CTest, GitHub Actions, MinGW/Wine local verification, and a native package audit.
- The current serial-only baseline has `SerialWritePort`, `SerialTransport`, neutral serial types, `Win32SerialPort`, and `SerialRtuTransport`.
- Main-window serial operations, the Modbus scan worker, and command-sequence writes use the current transport contracts.
- Contract, RTU adapter, Win32 serial, PTY, self-test, UI performance, package, and documentation checks passed before this workflow was created.

## Initial Scope Hypothesis

Plan a production-grade next iteration of the unified serial transport architecture. Preserve the current serial behavior and package constraints. Do not add TCP, UDP, network services, Qt, SQLite, plugins, or new runtime dependencies unless the user later explicitly changes scope.

## Product Direction Confirmed

The product is primarily a serial diagnostic tool for PLCs and other devices
that communicate over serial links. The immediate priority is a small, stable,
strong serial foundation; feature expansion follows after the foundation is
reliable. Future data scanning/matching must be able to describe non-fixed bit
layouts and multiple encodings, including Gray code, without coupling those
rules to the transport layer.

## Approved Direction

- Replace the broad transport facade with one serial session owner and direct
  migration of all in-repo callers.
- Use typed operation results, session generations, explicit deadlines, and a
  queue bounded by both request count and bytes.
- Keep RTU framing and future Gray-code/bit-layout decoding above the byte
  transport.
- Preserve the native build, PTY, MinGW/Wine, package, size, and security gates
  without adding runtime dependencies.

Phase 2 was approved on 2026-07-12. Phase 3 will write the detailed execution
plans; no project source code changes are authorized until Phase 4 approval.

## Planning Result

Phase 3 produced four phase plans and 18 detailed task plans. BS-6 moved RTU
migration after backend contract adoption and split main-window lifecycle from
main-window I/O to preserve buildability. Automated consistency checks passed;
the plans await explicit execution approval.

Execution was approved on 2026-07-12. Phase 4 starts from Phase 1, Task 1 and
must follow the recorded task order, verification gates, state updates, and
per-task commits.

Phase 1 completed on 2026-07-13 with all four contract-foundation tasks and all
27 host CTest targets passing. The user approved Phase 2, so execution now moves
to the sole Win32 session owner and direct caller migration; production queue
integration and exactly-once hardening remain in their planned later tasks.
