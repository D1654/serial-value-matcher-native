# Phase 2 Task 02 API Reference - Generation-Aware Win32 Settlement

Generated: 2026-07-13

## Research Status and Fallback

The dedicated task-level DeepWiki query repeatedly timed out during retry and
fallback attempts. No further network query was issued. This reference uses the
successful phase cache in `phase-2-research.md`, the Phase 1 contracts and test
suite, the current Win32 implementation, the Task 02 plan, and established
Win32 synchronous-I/O semantics available to the model.

Confidence is high for the C++ interface conflict, session/queue ownership,
generation, exactly-once settlement, and deterministic test design. Confidence
is medium for driver-specific serial cancellation latency; the implementation
must not promise more than the synchronous backend can prove.

## Current Contract Conflict

`Win32SerialSession` cannot directly inherit both the temporary
`SerialTransport` facade and the Phase 1 narrow interfaces. C++ cannot override
methods that have identical parameter lists but unrelated return types.

The conflicting methods include:

- `open(SerialOpenOptions)`: `bool` versus `SerialOperationResult`.
- `close()`: `void` versus `SerialOperationResult`.
- `setDataTerminalReady(bool)` and `setRequestToSend(bool)`: `bool` versus
  `SerialOperationResult`.
- `cancelPendingWrites()` and `takeCompletedWrites()`: legacy and typed result
  vectors with otherwise identical signatures.

Changing only covariant returns cannot solve these conflicts because the return
types are values, not related pointer or reference types.

## Minimum Capability-View Design

Keep `Win32SerialSession` temporarily derived from `SerialTransport` so existing
callers remain buildable through Tasks 2-5. Define one non-owning capability
view in `win32_serial_session.h/.cpp` that implements `SerialSession`,
`SerialByteStream`, and `SerialWriteScheduler` and holds only a pointer or
reference to the owner.

Expose the view as a `SerialSession&` from the owner. Its `byteStream()` and
`writeScheduler()` methods return the same view. Both the legacy facade methods
and the typed view methods must call the same private owner cores for lifecycle,
I/O admission, settlement, state, and line control.

The view must not own a COM handle, queue, worker, event, configuration, result
deque, or generation. It is a temporary compile-time bridge, not a second
transport implementation. Task 6 should remove the legacy facade and capability
view and then let `Win32SerialSession` directly implement the narrow contracts.

Do not maintain parallel legacy and typed state machines. Prefer one canonical
typed completion record, with optional diagnostic text retained only so the
temporary legacy API can be converted at its boundary.

## State and Generation Rules

Store `SerialSessionState`, a monotonic generation counter, and the currently
published generation inside the session synchronization boundary.

- Initial state is `Closed`, published generation is zero, and endpoint is
  empty.
- `open` transitions to `Opening` before native setup begins.
- A failed validation or native setup publishes no generation and transitions
  to `Faulted` with structured numeric evidence.
- A successful open increments the private counter and publishes the new
  nonzero generation only after the handle is configured and DTR/RTS setup has
  succeeded.
- Closing captures the old generation and endpoint for terminal evidence,
  changes the state to `Closing`, and invalidates the publicly visible
  generation before worker shutdown begins.
- Reopen occurs only after the old worker and active operation have settled and
  the old native handle has been released. It publishes a new generation and
  never replays old requests.

Old-generation settlement must compare the worker completion with the queue's
active reservation `(generation, requestId)`, not the already-invalidated
published generation. Otherwise close would reject its own legitimate terminal
completion and leak active accounting.

The write queue must not be reconstructed on close. Its request-ID counter is
monotonic and must survive reconnect, while pending and active reservations are
fully released.

## Queue Activation and Exactly-Once Settlement

Replace the Win32 worker's transitional `takeNext()` call with
`SerialWriteQueue::activateNext()`. The queue then owns the active reservation
metadata while the worker owns the moved payload buffer.

All active terminal paths must call `completeActive(requestId, generation, ...)`:

- full successful write;
- short or zero-byte write;
- native failure;
- expired deadline;
- explicit cancellation;
- session close;
- device disconnect.

`completeActive` already rejects duplicate, stale-generation, mismatched-ID,
nonterminal, and oversized-byte-count results without releasing the wrong
reservation. Publish a completion only when this call returns the matching
terminal result.

Remove `writeInProgress_` as an independent accounting source. Queue snapshots
already count pending and active requests and bytes. After activation,
`SerialWriteQueue::empty()` remains false because active work is counted, so the
worker must use `pendingCount()` when deciding whether another request is ready.

Close can settle pending requests immediately with `cancelAllPending()`. The
active request must be settled by the worker after its synchronous native call
has returned or been cancelled. An owner-side cancellation flag may record the
close reason without modifying the neutral queue contract.

## Synchronous Cancellation and Close Ordering

The current COM handle is opened without `FILE_FLAG_OVERLAPPED`, and the worker
uses synchronous `WriteFile`. `CancelIoEx` must not be treated as the primary
way to cancel that call or as proof that the operation has completed.

For the current backend, use `CancelSynchronousIo(writeThreadHandle)` to request
cancellation of synchronous I/O issued by the worker thread. `PurgeComm` with
`PURGE_TXABORT` may be used as a serial-driver abort request. `CancelIoEx` is a
future overlapped-I/O seam only unless the handle and operation are actually
converted to that model.

Required close order:

1. Under the session lock, change to `Closing`, capture the old generation,
   endpoint, handle, and worker handle, invalidate the published generation,
   reject new work, request worker stop, and settle pending requests.
2. Signal the worker wake event.
3. Outside the lock, request native cancellation with
   `CancelSynchronousIo`; optionally request `PurgeComm(PURGE_TXABORT)`.
4. Wait for the worker with `WaitForSingleObject` without holding the lock.
5. The worker records actual transferred bytes and publishes exactly one active
   terminal result through `completeActive` before releasing its local payload.
6. Close the worker handle only after the join.
7. Close the COM handle only after worker settlement and join.
8. Under the lock, clear public endpoint/options as required and transition to
   `Closed` with generation zero.

Cancellation request and terminal completion are separate events. Do not free
the active payload, clear the active reservation, destroy the wake event, or
close the COM handle merely because a cancellation API returned success.

The task dependency note should include `CancelSynchronousIo`; listing only
`CancelIoEx` is inconsistent with the preserved synchronous backend.

## Deadline Semantics and Limitations

The queue already computes and stores an absolute `steady_clock` deadline at
admission. The worker should inspect it immediately before issuing `WriteFile`
and again after each synchronous chunk returns.

- A request expired before native I/O starts settles as `Timeout` with zero
  transferred bytes.
- A call that returns after the absolute deadline settles as `Timeout` while
  preserving its actual transferred-byte count, including a late full write if
  that is the approved Phase 1 contract behavior.
- A native timeout code also maps to typed timeout evidence.
- The decision must not parse localized error text or depend solely on the
  port-wide `COMMTIMEOUTS` value.

This synchronous design provides deterministic deadline classification, not an
exact deadline-time interruption guarantee. A blocking driver call may return
later than the requested deadline. Precise deadline cancellation would require
an overlapped operation or a separately governed watchdog and is outside this
task. Do not describe Task 02 as implementing full overlapped I/O.

## Numeric Native Error Mapping

Capture `GetLastError()` immediately at the failed Win32 call boundary. Use a
private native outcome containing success, byte count, native code, typed
category, and optional diagnostic text. Map decisions from numeric codes and
operation context only.

Minimum stable mapping:

- `ERROR_SEM_TIMEOUT` and `ERROR_TIMEOUT` -> `SerialOperationStatus::Timeout`
  and `SerialErrorCategory::Timeout`.
- `ERROR_OPERATION_ABORTED` when session or operation cancellation was
  requested -> `Cancelled` and `Cancelled`.
- `ERROR_DEVICE_NOT_CONNECTED`, `ERROR_INVALID_HANDLE`, and other explicitly
  recognized device-loss codes -> `Disconnected` and `Disconnected`.
- Other nonzero Win32 failures -> `Failed` and `NativeFailure`.
- A short or zero-byte write without a native failure code -> `Failed` and
  `IoFailure`.
- Closed-state admission -> `RejectedClosed` and `SessionClosed` without
  assigning a request ID.

`win32SerialErrorText` remains useful for legacy/UI diagnostics, but functions
such as the current `isTimeoutErrorText` must not control transport behavior.
Every typed terminal result preserves operation kind, request ID, old session
generation, deadline status, endpoint, actual byte count, category, and native
code.

## Deterministic Tests Without Serial Hardware

`native_win32_serial_tests.cpp` can deterministically cover:

- compile-time proof that the temporary owner still satisfies
  `SerialTransport` and its returned capability view satisfies all three narrow
  Phase 1 interfaces;
- initial `Closed` snapshot with generation zero;
- typed closed, closing, and faulted rejection without queue mutation;
- invalid options producing `Opening` then `Faulted` with no generation
  publication;
- numeric error classification for timeout, cancellation, disconnect, generic
  native failure, and short write;
- pre-I/O and post-I/O deadline classification;
- generation increments and endpoint publication using a narrow friend test
  accessor or factored state helper that does not expose a production test API;
- synthetic queue admission/activation followed by close, exactly-once active
  and pending settlement, duplicate completion rejection, and stale-generation
  rejection without accounting mutation;
- request IDs remaining monotonic across synthetic close/reopen.

Tests must not require a real COM port, USB unplug event, driver-specific
`PurgeComm` behavior, or deterministic native cancellation latency under Wine.
Those behaviors remain hardware/integration evidence, not host-unit guarantees.

`native_reconnect_state_tests.cpp` should verify that reconnect retains all
requested serial options while replacing only the endpoint and that reconnect
state stores no payload or request ID for replay. Stale-generation completion
rejection belongs in `native_win32_serial_tests.cpp`; `NativeReconnectState`
does not own session generations, queues, or results and should not gain those
responsibilities merely to satisfy a test location.

## File Scope

The implementation can remain within the Task 02 source scope:

- `src/win32/win32_serial_session.h`
- `src/win32/win32_serial_session.cpp`
- `tests/native_win32_serial_tests.cpp`
- `tests/native_reconnect_state_tests.cpp`

No modification is required to `serial_types.h`, `serial_session.h`,
`serial_write_queue.h/.cpp`, `native_reconnect_state.h/.cpp`, or CMake if the
capability view and deterministic friend test accessor are declared within the
Win32 session files.

If the plan instead insists that `Win32SerialSession` itself directly inherit
both old and new interfaces during Task 02, the task is impossible without
broader caller and facade changes. The recommended capability view avoids that
scope expansion and preserves a clean Task 6 deletion point.
