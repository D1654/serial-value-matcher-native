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

Phase 2 completed on 2026-07-16. The broad transport facade, queue capability
inheritance, compatibility adapters, and localized transport error channel are
gone. `Win32SerialSession` directly owns the three narrow contracts, all active
callers and loopback tests use typed results, and the portable plus MinGW/Wine
test trees pass. Execution pauses at the Phase 2 milestone before Phase 3.

The user approved Phase 3 on 2026-07-16. Production hardening begins with real
session queue backpressure: counted requests and bytes must include active work,
reject immediately at fixed limits, and expose coherent typed snapshots to the
UI before exactly-once completion and evidence tasks proceed.

Phase 3 Task 1 completed on 2026-07-17. The production session now enforces the
64-request and 256 KiB budgets with generation-aware snapshots, active request
identity, per-generation high-water evidence, and immediate typed rejection.
The UI consumes the canonical snapshot directly, obsolete queue bypasses are
removed, and host, MinGW/Wine, PTY, self-test, UI-performance, package, and
documentation gates all pass.

Phase 3 Task 2 completed on 2026-07-17. Accepted writes now share one request-ID
sequence and publish each terminal state once across success, failure, timeout,
cancellation, disconnect, close, and reconnect. Effective write deadlines are
capped at one second. If a synchronous driver call does not settle within the
join target, the session publishes one logical terminal result but retains the
active queue reservation, worker, handle, and payload until native settlement;
late cleanup cannot publish again or replay the request. A permanently stuck
vendor driver remains a physical boundary: close/reopen stays unavailable and
destruction fails fast rather than releasing live native ownership. Host 27/27,
MinGW/Wine 34/34, the 5005-transaction PTY matrix, application self-tests,
strict package inspection, and documentation consistency all pass.

Phase 3 Task 3 completed on 2026-07-20. Serial operation results now carry
neutral direction, status, deadline, native error, communication-mask, and
driver-queue evidence through Win32 UI logs, local raw records, and exported
evidence bundles. Metadata-only events remain payload-free; explicit successful
TX/RX paths keep their existing payload evidence. The unused localized message
channel was removed from the write queue, and Modbus records every underlying
serial operation in order without classifying cancellation from Chinese text.
Shutdown and normal scan completion both settle pending evidence before state is
released. Host 27/27, MinGW/Wine 34/34, the 5005-transaction PTY matrix, strict
package inspection, UI self-test/performance, and documentation checks pass.

Phase 3 Task 4 completed on 2026-07-20. The Wine/PTTY harness now runs isolated
normal, reopen, timeout, pending-cancel, close, stale-generation, and stress
scenarios with two alternating Modbus fixtures. Timeout sends a real request;
close interrupts an active bounded write; cancellation is explicitly limited to
pending work; and exact stale-completion rejection remains a deterministic
generation test rather than a false claim about untagged serial bytes. The
harness restores COM mappings, serializes each Wine prefix, reaps children on
errors and signals, and records local-only evidence without promoting PTY results
to physical-driver or CI coverage. Host 27/27, MinGW/Wine 34/34, the final
5008-transaction matrix, self-test/UI performance, strict package, hash, and
documentation gates pass.

Phase 3 Task 5 completed on 2026-07-20. The existing CMake graph already
registered every hardened serial source and test, so no duplicate target or
compatibility code was added. Both Windows workflows now reject empty CTest
trees, package evidence proves the transport-v2 test set and fixed size gates,
and the retained backend closure records queue, exactly-once, typed-error,
generation, and PTY-fault coverage. The seven-scenario PTY matrix remains
explicitly local-only with `CiExecutesPtyMatrix=no`. MinGW packaging rejects
malformed Wine gate flags, and the consistency checker now protects CMake,
workflow, package, trigger, and all active-document PTY terms. Host 27/27,
MinGW/Wine 34/34, the 5006-transaction PTY matrix, strict self-test/UI
performance, package inspection, SHA256, documentation checks, and independent
review all pass.
