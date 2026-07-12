# Competitive Analysis: Unified Serial Transport v2

Generated: 2026-07-11
Scope: Windows-native serial transport architecture only. No TCP, UDP, Qt, SQLite, plugins, or new runtime dependencies.

## Executive Conclusions

Mature serial stacks converge on the same boundaries even when their APIs differ:

1. A single owner controls the device handle and all platform I/O. UI code and protocol code submit work or observe state; they do not race the handle.
2. Configuration/lifecycle, byte-stream I/O, write scheduling, and protocol transactions are distinct responsibilities. A compatibility facade may combine them, but the internal contracts stay narrow.
3. "Accepted" means queued, not physically transmitted. Every operation needs a correlation id and a terminal result that distinguishes sent, partial/failed, timed out, cancelled, closed, and rejected.
4. Deadlines and cancellation are first-class. Closing or reconnecting must drain or settle all outstanding work before a handle is reused.
5. Reconnect is an explicit state machine with an endpoint/session generation. Stale completions from the previous handle must not update the new session.
6. Evidence is part of the transport contract: direction, request id, endpoint, timestamps, byte counts, outcome, and platform error data must be observable without coupling the transport to the UI.

The current project has the right starting point (`SerialWritePort`, `SerialTransport`, `Win32SerialPort`, `SerialRtuTransport`, fake transport tests, and the PTY matrix). The next optimization should split the broad facade internally and preserve it as a migration boundary rather than adding another concrete backend.

## Sources And Observed Patterns

The research pass covered four primary API/design searches: Win32 overlapped serial I/O and cancellation, pySerial timeout/cancellation semantics, jSerialComm event/timeout semantics, and libmodbus RTU timeout/error-recovery semantics. Product references were also checked for user-facing expectations in Windows terminals and Modbus diagnostic clients.

### Win32 native I/O

- Microsoft `ReadFile`/`WriteFile` document `ERROR_IO_PENDING` for overlapped handles; completion must be observed through the `OVERLAPPED` object rather than inferred from the initial call.
- `CancelIoEx` can cancel operations issued by another thread, but cancellation still requires waiting for the completion/cleanup path before closing or reusing the handle.
- `COMMTIMEOUTS`, `ClearCommError`, and `PurgeComm` make timeout, modem/queue status, and buffer discard explicit platform operations.
- The practical lesson is a worker-owned handle, explicit operation state, monotonic deadlines, and a join/drain phase during close/reopen. A UI timer polling a shared handle is not an equivalent ownership model.

References:

- [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
- [ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile)
- [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)
- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [COMMTIMEOUTS](https://learn.microsoft.com/en-us/windows/win32/api/commapi/ns-commapi-commtimeouts)
- [ClearCommError](https://learn.microsoft.com/en-us/windows/win32/api/commapi/nf-commapi-clearcommerror)

### Mature serial libraries

- **pySerial** exposes separate read timeout and write timeout controls, `in_waiting`/`out_waiting`, and cross-thread `cancel_read()`/`cancel_write()`. This makes partial data and cancellation visible to callers instead of hiding them in a single blocking call.
- **jSerialComm** separates port configuration from timeout mode and data-listener registration. Its event-oriented path lets an application keep the port object in one owner while consumers subscribe to received data and error events.
- **Qt SerialPort** (used here only as a behavioral reference, not a runtime dependency) bounds its write buffer when requested, reports `bytesWritten` separately from write acceptance, and cancels pending I/O during close. The distinction between accepted, completed, and cancelled writes is important for a native queue.

References:

- [pySerial API](https://pyserial.readthedocs.io/en/latest/pyserial_api.html)
- [jSerialComm project documentation](https://fazecast.github.io/jSerialComm/)
- [jSerialComm `SerialPort` API](https://fazecast.github.io/jSerialComm/javadoc/com/fazecast/jSerialComm/SerialPort.html)
- [Qt SerialPort API](https://doc.qt.io/qt-6/qserialport.html)
- [Qt SerialPort source](https://github.com/qt/qtserialport)

### Modbus diagnostic clients and protocol libraries

- **libmodbus** keeps an RTU context separate from the transport backend and provides independent response and inter-byte timeouts. Its error-recovery flags make link/protocol recovery a policy choice rather than an implicit retry loop.
- **Modbus Poll** and **QModMaster** establish user expectations for a diagnostic client: visible serial parameters, request/response timing, function/address context, readable exception text, and a monitor/log that can be saved or inspected after a failure.
- **RealTerm** and **Tera Term** establish the Windows-terminal baseline: raw ASCII/hex views, timestamps, explicit open/close state, repeatable send actions, and persistent logs. Users expect a failed write or unplugged device to be diagnosable from the visible record, not only from a transient status label.

References:

- [libmodbus reference](https://libmodbus.org/reference/)
- [libmodbus RTU backend](https://github.com/stephane/libmodbus/tree/master/src)
- [Modbus Poll](https://www.modbustools.com/modbus_poll.html)
- [QModMaster project](https://sourceforge.net/projects/qmodmaster/)
- [RealTerm project](https://sourceforge.net/projects/realterm/)
- [Tera Term manual](https://teratermproject.github.io/manual/5/en/)

## Common Expectations And Pain Points

| Concern | Mature-tool expectation | Failure mode to avoid in v2 |
|---|---|---|
| Reconnect | Explicit disconnected/reconnecting/connected state, visible attempt outcome, preserved port profile | Hidden reopen from a UI timer; old completions mutate the new connection |
| Cancellable transaction | A request can be cancelled by id/deadline; cancellation settles with a terminal result | A boolean flag is checked only between reads, leaving a blocked I/O or stale queue item |
| Write queue | Bounded by count and/or bytes; enqueue is distinct from sent; full/rejected is deterministic | Unbounded memory growth or reporting "sent" when only copied into a buffer |
| Partial write/read | Byte count and remainder are available to the caller; framing owns accumulation | Treating a short write or partial frame as success, or silently dropping remainder bytes |
| UI ownership | One worker/session owns the handle; UI receives snapshots/events | Main window, protocol worker, and reconnect code each call platform methods directly |
| Telemetry | Request id, direction, endpoint, timestamps, timeout, byte count, error category/code | Logs show payloads without operation context, making reconnect and timeout failures impossible to correlate |
| Modbus transaction | Request/response, RTU frame, CRC/exception, retry policy, and timeout are separately visible | Protocol adapter reimplements queue, lifecycle, and retry behavior independently |
| Testing | Fake contract tests plus PTY loopback and hardware acceptance; deterministic timeout/cancel races | Only happy-path mocks or only real hardware, so queue saturation and unplug races remain untested |

## Current Baseline Gaps

The codebase already routes production consumers through `SerialTransport`, but the facade still combines responsibilities that should be independently testable:

- `src/transport/serial_transport.h` includes lifecycle, direct byte writes, queued writes, completion polling, read waiting, and receive draining in one interface.
- `SerialWriteResult` has useful status and byte count fields, but lacks a stable operation timestamp/deadline, endpoint/session generation, platform error code, and an explicit distinction between partial-send and failed-before-send.
- `waitForReadyRead()` plus `readAvailable()` has no transport-level cancellation/deadline token or structured read result. Protocol code must infer timeout versus closed/error from a boolean and a later error string.
- `Win32SerialPort` owns a worker and queue, while reconnect policy lives in `NativeReconnectState` and orchestration lives in the main window. The ownership direction is workable, but the lifecycle/session boundary is not yet a first-class service.
- Queue capacity is count-based. A few very large payloads and many small payloads consume the same unit, so backpressure does not express memory or device-time pressure.
- Evidence is currently assembled by higher layers. The transport should emit a small, UI-neutral event record so raw I/O, Modbus attempts, command sequences, and reconnects share one correlation model.
- Existing fake, Win32, adapter, and PTY tests are a strong base, but the next gates should cover cancellation races, close/reopen while a write is in flight, queue byte limits, stale completion suppression, and deterministic error-code mapping.

## Actionable Architecture Lessons

### 1. Split contracts behind a compatibility facade

Introduce pure C++ contracts with one responsibility each:

- `SerialSession`: open/close/state, endpoint identity, session generation, and close-drain completion.
- `SerialByteStream`: synchronous/worker-owned read and write of bytes with structured `IoResult` and a monotonic deadline.
- `SerialWriteScheduler`: bounded enqueue, cancellation by request id, completion retrieval, and queue watermarks.
- `SerialTransportObserver` or `SerialEvidenceSink`: non-blocking operation/state events; no Win32 or UI types.

Keep `SerialTransport` as a source-compatible facade during migration. `Win32SerialPort` composes the four roles; `SerialRtuTransport` depends only on byte-stream/transaction primitives and never on the UI or queue implementation.

### 2. Make operation state and cancellation explicit

Use one request record with `requestId`, `sessionGeneration`, direction, payload length, enqueue/start/finish timestamps, deadline, cancellation source, and terminal status. Define invariants:

- every accepted request settles exactly once;
- accepted does not imply transmitted;
- timeout/cancel/close cannot later become sent in a newer session;
- partial byte counts are retained;
- `close()` drains or marks all pending and in-flight work before the handle is released.

Prefer a deadline/cancellation object passed through the worker and protocol adapter over scattered `shouldCancel` callbacks. Preserve the existing callback as an adapter until callers migrate.

### 3. Treat reconnect as a session state machine

The session owner should publish `Closed`, `Opening`, `Open`, `Reconnecting`, and `Faulted` snapshots with attempt number, next retry time, and reason. Increment a generation on every successful open. Results carrying an older generation are discarded or marked stale. Keep automatic retry policy configurable and observable; never silently reopen while a command sequence or Modbus scan still owns the previous transaction.

### 4. Define backpressure in bytes and time

Retain a count limit for compatibility, then add a maximum pending-byte budget and per-request deadline. Return `RejectedFull` before copying an over-budget payload. Expose pending count, pending bytes, oldest age, active request id, and high-water mark in the snapshot. This gives the UI a truthful reason to stop or slow file sends.

### 5. Keep protocol adaptation narrow

RTU framing, CRC, response matching, and Modbus retry policy belong above the byte stream. The adapter should receive a request/response transaction primitive and return structured protocol outcomes. It must not open ports, own queues, or decide UI reconnect policy. This prevents the next protocol extension from creating another transport implementation.

### 6. Make evidence a stable transport output

Emit bounded, UI-neutral events with: session generation, endpoint, operation id, direction, payload byte count (and optional redacted/raw payload according to the existing logging policy), monotonic and UTC timestamps, timeout/deadline, status, partial byte count, and native error code/message. Higher layers can render logs and Markdown reports without reimplementing correlation. Counters should include opens, reconnect attempts, queue rejection, cancellation, timeout, partial I/O, and stale completions.

### 7. Use a layered regression strategy

1. Contract tests: fake session/byte-stream/scheduler combinations, including every terminal state and exactly-once completion.
2. Scheduler tests: FIFO ordering, count/byte limits, cancellation of queued and active work, close drain, and request-id reuse prevention.
3. Adapter tests: chunked RTU frames, inter-byte timeout, CRC/exception, cancellation, and stale generation.
4. Win32/PTY tests: normal, reopen, timeout, cancel, stress, unplug/reopen, and queue saturation through the interface only.
5. Release evidence: MinGW/Wine tests, package audit, SHA256, UI self-test/performance, and a documented real-device acceptance matrix.

## Recommended Scope And Order

| Priority | Work package | Exit criterion |
|---|---|---|
| P0 | Freeze current facade behavior and write invariants | Existing 27+ tests remain green; accepted/sent/cancelled semantics are documented |
| P1 | Add narrow session, byte-stream, scheduler, and evidence interfaces | Win32 implementation composes them; existing callers still use `SerialTransport` |
| P2 | Move all handle access behind one session worker | UI, Modbus, command sequence, and reconnect code contain no direct platform I/O |
| P3 | Add generation-aware reconnect and byte/time backpressure | Stale completions, close drain, queue full, and reopen races have deterministic results |
| P4 | Add structured transport events and counters | Raw I/O and protocol reports correlate by operation/session id |
| P5 | Expand deterministic, PTY, and release gates | New failure matrix is automated without hardware; hardware acceptance remains documented |

## Guardrails

- Do not add TCP/UDP or a generic network abstraction in this iteration; the abstraction should be transport-neutral in naming but have only a serial implementation.
- Do not restore Qt or add another runtime/library dependency.
- Do not let the UI become the transport scheduler. UI actions should enqueue commands and consume snapshots/events.
- Do not change Modbus retry or report semantics until the new transaction contract has compatibility tests.
- Keep `SerialTransport` as a migration facade until all consumers use the narrower contracts and the release evidence proves parity.

## Local Baseline References

- `src/transport/serial_transport.h`
- `src/transport/serial_write_queue.h`
- `src/transport/serial_rtu_transport.h`
- `src/win32/win32_serial_port.h`
- `src/win32/native_reconnect_state.h`
- `tests/transport_contract_tests.cpp`
- `tests/native_modbus_transport_adapter_tests.cpp`
- `tests/native_win32_serial_loopback_tests.cpp`
- `scripts/run-windows-native-serial-pty-loopback.py`
