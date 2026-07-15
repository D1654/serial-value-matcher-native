# Phase 2 Task 04 API Reference - Generation-Aware Main-Window I/O

Generated: 2026-07-14

## Research Status and Fallback

The dedicated Task 04 research ran one focused DeepWiki `ask` query for every
Windows API named by the task plan:

- `ReadFile`
- `WriteFile`
- `ClearCommError`
- `WaitForSingleObject`
- `SetEvent`
- `CancelIoEx`

All six calls completed against `microsoft/Windows-classic-samples`.
`ReadFile`, `WriteFile`, `WaitForSingleObject`, and `SetEvent` returned useful
generic sample evidence. The repository did not directly document
`ClearCommError` or `CancelIoEx`; its `CancelSynchronousIo` sample was useful
only as a related cancellation pattern. The required fallback sequence was
therefore applied:

1. DeepWiki `contents` completed, but still contained no exact serial contract
   for the two missing APIs.
2. Web search and direct-page access were attempted, but the browsing service
   repeatedly returned a response-decoding error.
3. Exact declarations were verified in the installed MinGW headers, and API
   semantics were corrected from the MicrosoftDocs `sdk-api` source pages.
4. C++ standard-library behavior was analyzed from the C++20 contract and the
   project's current use; no external repository is required for it.

The Microsoft API contract is authoritative where DeepWiki's generated answer
was incomplete or imprecise. In particular, an overlapped `ReadFile` or
`WriteFile` that is merely pending returns `FALSE` with `ERROR_IO_PENDING`; a
successful return from `CancelIoEx` requests cancellation but does not prove
terminal completion.

Confidence is **high** for signatures, ownership, return-value checks, current
encapsulation, generation/request matching, queue accounting, and the
synchronous-backend boundary. Confidence is **medium** for USB-to-serial
driver timing after cancellation or unplug; Task 04 must expose only observed
typed results and must not promise a driver-specific cancellation latency.

## Ownership and Existing Encapsulation

| API | Required owner | Already encapsulated? | Task 04 rule |
|---|---|---|---|
| `ReadFile` | `Win32SerialSession` | Yes. Called only by `readOperation` after generation validation and `ClearCommError`. | Main-window code calls `SerialByteStream::readAvailable`; it never receives a COM `HANDLE`, buffer lease, or `DWORD` count. |
| `WriteFile` | `Win32SerialSession` | Yes. Called only by `writeBytesToHandle`, used by synchronous and queued typed write paths. | Main-window code calls `SerialWriteScheduler::enqueueWrite`; it never loops native partial writes. |
| `ClearCommError` | `Win32SerialSession` | Yes. Used by the legacy readiness path and typed `readOperation`. | Task 04 removes the UI's readiness-facade sequence and consumes one typed read result instead. |
| `WaitForSingleObject` | `Win32SerialSession` for serial worker/event handles | Yes for the serial worker and wake event. A separate Modbus UI thread wait exists but is not COM-handle I/O. | Do not add a serial wait to the UI thread. Worker joining and wake-event waiting remain private to the owner. |
| `SetEvent` | `Win32SerialSession` | Yes. Used to wake/stop the write worker after admission, cancellation, disconnect, or close. | Queue admission and cancellation go through the scheduler; the UI never signals the worker event. |
| `CancelIoEx` | Future overlapped backend owner | No call exists. The current COM handle and native reads/writes are synchronous. Active close cancellation uses session-private `CancelSynchronousIo` plus `PurgeComm`; pending queue cancellation is typed and native-I/O-free. | Do not introduce `CancelIoEx` in Task 04. It is a future seam, not a UI cancellation mechanism and not a substitute for current synchronous settlement. |

The temporary `CapabilityView` already exposes the narrow operations through
the same concrete owner:

- `SerialSession::byteStream()` returns a borrowed `SerialByteStream&`.
- `SerialSession::writeScheduler()` returns a borrowed
  `SerialWriteScheduler&`.
- Both references point back to the one `Win32SerialSession`; neither owns a
  handle, queue, event, worker, generation, buffer, or completion deque.
- Task 04 may store non-owning capability references beside `serialLifecycle_`
  or obtain them locally. It must not create another adapter or preserve I/O
  through `SerialTransport` compatibility calls.

## Windows API Contracts

### `ReadFile`

```cpp
BOOL WINAPI ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped);
```

Parameters:

- `hFile` is an open file/device handle with read access. A truly asynchronous
  call requires a handle opened with `FILE_FLAG_OVERLAPPED`.
- `lpBuffer` points to writable storage. It must remain valid and must not be
  read, written, reallocated, or freed until the operation is terminal.
- `nNumberOfBytesToRead` is the maximum requested byte count.
- `lpNumberOfBytesRead` receives the synchronous transferred count. `ReadFile`
  sets it to zero before work or error checking. It can be `NULL` only when
  `lpOverlapped` is non-null; for an overlapped call it should be `NULL` and the
  final count should come from the completion mechanism. Windows 7 required a
  non-null pointer even for overlapped calls.
- `lpOverlapped` is required, valid, and unique for a handle opened with
  `FILE_FLAG_OVERLAPPED`; otherwise it can be `NULL`. COM ports do not use file
  byte offsets, but the structure and any event it owns must remain valid and
  unmodified until final completion.

Returns and errors:

- Nonzero means the operation succeeded. The returned count may be less than
  requested and may be zero.
- Zero means failure or asynchronous pending completion. Call `GetLastError`
  immediately on that path.
- `ERROR_IO_PENDING` is successful asynchronous admission, not terminal
  failure. The caller must collect completion and the actual byte count.
- A cancelled operation completes with `ERROR_OPERATION_ABORTED`. Pending
  asynchronous operations use `CancelIo`/`CancelIoEx`; pending synchronous
  operations use `CancelSynchronousIo`.
- Invalid/closed handles fail, commonly with `ERROR_INVALID_HANDLE`; the
  session maps recognized device-loss codes to `Disconnected`.
- For a communications resource, `COMMTIMEOUTS` determines read behavior. A
  successful zero-byte read is not universal proof of disconnect and must not
  trigger reconnect by itself.
- A partial successful read is valid. Only bytes actually reported may be
  published; no uninitialized tail may reach the UI.

Task 04 application:

- The current backend is synchronous: it passes a `DWORD` result pointer and
  `lpOverlapped == nullptr`.
- `readOperation(maxBytes, deadline)` first obtains `cbInQue`, caps the read to
  `maxBytes`, the configured read buffer size, and currently queued bytes, then
  returns an owning `SerialReadResult`.
- The UI should call the typed operation directly. `Succeeded` plus an empty
  byte vector means "no data in this poll," not failure. `Timeout`,
  `Cancelled`, `Disconnected`, and `Failed` remain distinct typed outcomes.
- An absolute `SerialDeadline` can classify a late synchronous result but does
  not by itself interrupt a blocking driver call at the exact deadline.

### `WriteFile`

```cpp
BOOL WINAPI WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped);
```

Parameters:

- `hFile` is an open file/device handle with write access. Asynchronous use
  requires `FILE_FLAG_OVERLAPPED`.
- `lpBuffer` points to the bytes to send and must remain valid and unchanged
  until terminal completion.
- `nNumberOfBytesToWrite` is the requested count. Zero describes a technology-
  dependent null write; the typed serial contract rejects empty payloads before
  reaching this API.
- `lpNumberOfBytesWritten` receives the synchronous count and is initialized to
  zero by the API before work/error checking. It can be `NULL` only with a
  non-null `lpOverlapped`; overlapped completion obtains the final count later.
- `lpOverlapped` follows the same handle-mode, uniqueness, and lifetime rules
  as `ReadFile`. COM offsets are ignored.

Returns and errors:

- Nonzero means success. The reported count is the only valid transferred
  count and can be shorter than requested for a device.
- Zero means failure or asynchronous pending completion; capture
  `GetLastError()` immediately.
- `ERROR_IO_PENDING` means an overlapped write was admitted and remains
  pending. It requires later completion collection.
- Cancellation completes as `ERROR_OPERATION_ABORTED` when cancellation wins
  the race. The operation may instead complete normally or with another error.
- Invalid/closed handles fail, commonly with `ERROR_INVALID_HANDLE`.
- Communication write timeouts are governed by `COMMTIMEOUTS`; the typed
  absolute deadline is additional classification, not an exact synchronous
  cancellation guarantee.
- On a successful short write, the caller must continue with the unwritten
  suffix or return a structured partial result. A zero-byte success while bytes
  remain must not spin forever.

Task 04 application:

- `writeBytesToHandle` already loops synchronous `WriteFile` calls, preserves
  bytes completed by earlier chunks, and treats a zero-byte progress result as
  `IoFailure`.
- The scheduler owns the moved payload until the active request reaches one
  terminal result. UI records may retain their own copy for raw evidence, but
  they never lend the native worker a UI-owned pointer.
- Admission success is not physical-send success. Raw Tx evidence, history,
  progress, and Tx byte counters update only from the matching terminal
  `Succeeded` result.

### `ClearCommError`

```cpp
BOOL WINAPI ClearCommError(
    HANDLE hFile,
    LPDWORD lpErrors,
    LPCOMSTAT lpStat);
```

Parameters:

- `hFile` is the open communications-resource handle.
- `lpErrors` is optional output storage for a communications-error bitmask.
- `lpStat` is optional output storage for a point-in-time `COMSTAT` snapshot.
  Both outputs are caller-owned and need remain valid only for the synchronous
  call.

Returns and errors:

- Nonzero means the query/clear operation succeeded. A nonzero `*lpErrors` on
  this path is a reported communications condition, not a failed API call.
- Zero means the API call failed. Capture `GetLastError()` immediately; do not
  inspect a stale error value after success.
- Reported mask bits are `CE_RXOVER` (`0x0001`, receive-buffer overflow),
  `CE_OVERRUN` (`0x0002`, character-buffer overrun), `CE_RXPARITY` (`0x0004`),
  `CE_FRAME` (`0x0008`), and `CE_BREAK` (`0x0010`). Multiple bits may coexist.
- If `DCB::fAbortOnError` is `TRUE`, a communications error terminates existing
  reads/writes and rejects new ones until `ClearCommError` acknowledges it. The
  current session configures `fAbortOnError = FALSE`, so Task 04 must not invent
  a different recovery policy.

Important `COMSTAT` fields:

- `fCtsHold`, `fDsrHold`, and `fRlsdHold`: output transmission is waiting for
  the corresponding modem signal.
- `fXoffHold`: transmission waits because an XOFF was received.
- `fXoffSent`: reception waits because XOFF was sent.
- `fEof`: the EOF character was received.
- `fTxim`: a character queued by `TransmitCommChar` is waiting ahead of normal
  output.
- `cbInQue`: bytes received by the provider but not yet read by this process.
- `cbOutQue`: user-data bytes remaining for transmission. For a synchronous
  nonoverlapped write it is not a durable completion counter and can be zero
  when the call returns.

Task 04 application:

- `COMSTAT` is a momentary snapshot. Another device event or I/O call can make
  `cbInQue` stale immediately, so `ReadFile` may still return fewer or zero
  bytes without implying disconnect.
- The session already serializes `ClearCommError` and `ReadFile` behind its I/O
  lock and validates the generation before and after the lease. The UI must not
  reproduce the current `waitForReadyRead` plus `readAvailable` two-call race.
- A successful API call with nonzero line-error bits maps to structured
  `IoFailure`; a failed API call retains its numeric native code and typed
  category. UI branches never parse `commStatusErrorText` or `lastErrorText`.

### `WaitForSingleObject`

```cpp
DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
```

Parameters:

- `hHandle` must identify a waitable object and grant `SYNCHRONIZE`. Task 04's
  relevant objects are the session-owned worker-thread and wake-event handles.
- Closing the handle while a wait is pending is undefined behavior.
- `dwMilliseconds == 0` polls, a finite value bounds the wait, and `INFINITE`
  waits until signaled. On Windows 8 and newer, time spent in a low-power state
  is not counted toward a finite timeout.

Returns and errors:

- `WAIT_OBJECT_0` (`0x00000000`) means the object is signaled. For a thread,
  the thread terminated; for the auto-reset wake event, one waiter consumes the
  signal.
- `WAIT_TIMEOUT` (`0x00000102`) means the interval elapsed while nonsignaled.
- `WAIT_ABANDONED` (`0x00000080`) applies to an abandoned mutex and grants its
  ownership to the waiter; it is not an expected thread/event result.
- `WAIT_FAILED` (`0xFFFFFFFF`) means failure. Only then is `GetLastError()` the
  extended wait error.
- A window-creating/UI thread must continue pumping messages; an infinite
  direct wait can deadlock broadcasts. Use a message-aware wait when such a
  thread truly must wait.

Task 04 application:

- Serial waits remain entirely in `Win32SerialSession`; the main-window polling
  timer performs a bounded typed read and never waits on a serial worker/event.
- Close must signal/cancel, wait for worker termination, observe terminal
  settlement, and only then close thread/event/device handles.
- The existing session currently ignores the wait return values. Task 04 must
  not duplicate or compensate for that in UI code; any stronger wait-failure
  classification belongs at the session-owner boundary.

### `SetEvent`

```cpp
BOOL WINAPI SetEvent(HANDLE hEvent);
```

Parameters and behavior:

- `hEvent` is a valid event-object handle with `EVENT_MODIFY_STATE` access.
- Nonzero means the event was set. Zero means failure; capture
  `GetLastError()` immediately. An invalid or already-closed handle fails,
  commonly with `ERROR_INVALID_HANDLE`.
- Setting an already-signaled event has no additional effect.
- A manual-reset event remains signaled until `ResetEvent` and can release all
  current/subsequent waiters.
- An auto-reset event releases one waiter and resets automatically; if no
  waiter exists, it remains signaled until one consumes the signal.
- The event handle must outlive all threads that can signal or wait on it.

Task 04 application:

- The session creates `writeWakeEvent_` as unnamed, initially nonsignaled, and
  auto-reset. Coalesced wakeups are safe because the worker rechecks queue and
  stop state under the session lock.
- Admission, cancellation, disconnect, and close use `SetEvent` only through
  the session. The UI requests scheduler operations and never observes or
  signals the event directly.
- The existing session does not surface a rare `SetEvent` failure. UI code must
  not add a second wake path; exactly-once remediation, if needed, belongs in
  the owner rather than a compatibility facade.

### `CancelIoEx`

```cpp
BOOL WINAPI CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped);
```

Parameters:

- `hFile` identifies the file/device handle whose outstanding operations were
  issued by the current process.
- `lpOverlapped == NULL` marks all outstanding operations on that handle for
  cancellation, regardless of which process thread issued them.
- A non-null `lpOverlapped` marks operations issued for that handle with that
  exact `OVERLAPPED` object. The structure must still be alive and must not be
  reused.

Returns, races, and errors:

- Nonzero means the cancellation request was successfully issued. It does not
  mean the I/O is terminal and does not prove that cancellation won the race.
- Zero means failure; call `GetLastError()` immediately.
- `ERROR_NOT_FOUND` means there was no matching outstanding request to mark.
  This can be a benign race with normal completion and requires checking the
  operation's actual terminal result.
- An invalid/closed handle fails, commonly with `ERROR_INVALID_HANDLE`.
- A marked operation can still complete normally, complete cancelled with
  `ERROR_OPERATION_ABORTED`, or fail with another error. Completion must be
  collected through `GetOverlappedResult`, a completion port/routine, or the
  backend's equivalent settlement path.
- The buffer, `OVERLAPPED`, event, accounting reservation, and relevant handle
  lifetime must remain valid until the terminal completion is observed.
- `CancelIoEx` does not wait and does not change the handle's state.

Task 04 application:

- The current serial handle is not opened with `FILE_FLAG_OVERLAPPED`, and the
  backend calls `ReadFile`/`WriteFile` with `lpOverlapped == nullptr`.
  `CancelSynchronousIo(writeThread_)`, not `CancelIoEx`, is the current active
  write cancellation request during close.
- `SerialWriteScheduler::cancelPendingWrites()` cancels queue entries that have
  not become active; it does not cancel a native `WriteFile`. Its terminal
  results are returned synchronously.
- Task 04 must not add `CancelIoEx`, expose an `OVERLAPPED`, or claim exact
  deadline-time interruption. A future overlapped rewrite must be separately
  planned and must settle completion before buffer reuse or handle close.

## C++20 Standard-Library Guidance

### `std::vector`

- `std::vector<std::uint8_t>` owns a contiguous byte buffer. Moving it into
  `enqueueWrite` transfers ownership without exposing a native pointer to the
  UI after admission.
- A pointer returned by `data()` is invalidated by destruction and by
  reallocating mutations. The worker therefore uses the moved queue request's
  buffer only while that request remains alive.
- `SerialReadResult::bytes` owns its received data. The UI may move it into its
  bounded merge/evidence aggregation without borrowing session storage.
- Preserve the existing UI evidence copy before moving the write payload into
  the scheduler. Do not add a second transport buffer or compatibility copy.

### `std::deque`

- `std::deque<NativePendingSerialWrite>` fits the small ordered UI pending set.
  It supports insertion/removal without requiring contiguous storage.
- Do not retain iterators across a deque mutation. The existing pattern of
  finding, moving the record, and immediately erasing that iterator is valid.
- Queue capacity is 64 requests, so a linear UI match is bounded; an unordered
  index would add unnecessary code unless profiling proves a need.

### `std::optional`

- `std::optional` expresses an absent deadline, inactive queue reservation, or
  absent reconnect/configuration value without sentinel pointers.
- `SerialDeadline::expiresAt` uses `std::chrono::steady_clock`, so wall-clock
  adjustments cannot move an operation deadline.
- Check `has_value()`/`set()` before dereference and copy the value needed for a
  decision. Do not retain a pointer/reference into an optional owned by mutable
  session state.

### `std::find_if`

- `std::find_if(first, last, predicate)` returns the first matching iterator or
  `last`. The Task 04 predicate must compare both generation and request ID.
- Complexity is linear in the inspected range and remains bounded by the
  session's 64-request admission limit.
- A result is destructively removed from the session completion deque by
  `takeCompletedWrites()`. The UI must consume it once, find exactly one pending
  pair, erase that record once, and never replay its side effects.

### `std::uint64_t`

- `SerialOperationId` and `SerialSessionGeneration` are aliases of
  `std::uint64_t`; zero is explicitly unassigned.
- Preserve the full width in UI pending records and comparisons. Do not cast
  either identity to `DWORD`, `int`, a control item ID, or text-derived state.
- IDs and generations form two separate dimensions. Request ID alone is not a
  session identity and cannot protect a reconnect boundary.

## Typed Task 04 Migration Rules

### Pending identity and terminal processing

1. Add `generation` to `NativePendingSerialWrite` and keep the admitted
   operation's exact `(generation, requestId)` pair. Record neither field before
   `SerialWriteAdmissionResult::accepted()` succeeds.
2. A typed admission must also have a nonzero assigned request ID and a nonzero
   generation. Use the result's identity, not a later mutable session snapshot.
3. Drain `SerialWriteScheduler::takeCompletedWrites()` exactly once per UI
   polling pass. For each result, locate the pending record by the full pair.
4. If a matching old-generation result arrives after reconnect, erase only its
   old internal pending record, then suppress status, history, evidence,
   progress, counters, and reconnect effects. It is settled backend work, not
   replacement-session work.
5. Only a matching current-generation terminal `Succeeded` result may publish
   Tx evidence/history/progress/counters. Cancellation/failure text is derived
   from typed status/category/native code.
6. A duplicate or unknown terminal result has no UI effects and must not remove
   a different pending record.

### Cancellation result path

`SerialWriteScheduler::cancelPendingWrites()` currently returns its pending
terminal cancellations directly and does **not** publish those same results to
`takeCompletedWrites()`. Task 04 must not discard that vector and then wait for
results that will never reappear.

Use one terminal-result processing helper for both direct cancellation results
and asynchronously drained completions, or erase only the exact returned
`(generation, requestId)` records through equivalent logic. The file-send owner
starts only with an empty queue and excludes manual/Modbus admission, so the
current cancel-all-pending scheduler contract can represent file cancellation
only while the UI verifies that the records belong to that file-send ownership
and generation. It must not cancel or erase work admitted by a replacement
generation.

The active request is different from a pending request. Stopping file send can
cancel pending chunks, but it does not claim that a synchronous `WriteFile`
already active in the worker was interrupted. Its eventual terminal result is
still drained and suppressed if the UI deliberately released that file-send
presentation state.

### Queue snapshot and arbitration

`SerialWriteQueueSnapshot` already reports:

- `pendingCount` and `activeCount`
- `pendingBytes` and `activeBytes`
- request and byte capacities
- `countedCount()`, `countedBytes()`, `empty()`, and `full()`

The current `NativeSerialIoState::updateWriteQueueStatus(snapshot)` copies only
`pendingCount` and request capacity. That loses active work and the 256 KiB byte
budget. Task 04's planned active-accounting tests require the UI state to use
the counted values (and either retain counted bytes/byte capacity or retain the
snapshot's full/backpressure decision). An active request must continue to
block exclusive owners and count toward queue presentation until its terminal
result releases the reservation.

Immediate `RejectedFull` can also occur when the next payload exceeds remaining
byte capacity even if the queue is not globally `full()`. Admission result is
therefore authoritative; UI prechecks are advisory only.

### Receive polling

- Replace `isOpen` + `waitForReadyRead` + `lastErrorText` + `readAvailable` with
  a typed session snapshot and `SerialByteStream::readAvailable` calls.
- Keep the current bounds: at most eight batches per poll and at most 4096 bytes
  requested per batch unless the task plan explicitly changes them.
- `Succeeded` with bytes preserves raw-event batching and payload logging.
- `Succeeded` with no bytes and routine `Timeout` end the current poll quietly.
- `Disconnected` drives the existing lifecycle failure/reconnect path once.
  `Cancelled`/`RejectedClosed` caused by close or generation change do not
  mutate a new session.
- Other `Failed` results use structured category/native evidence for UI text.
  `lastErrorText` is never a branch condition.

### Manual and file enqueue

- Compute an explicit absolute deadline using `steady_clock` and the accepted
  serial write-timeout policy, then move the payload into
  `SerialWriteScheduler::enqueueWrite`.
- On acceptance, retain the exact returned identity and deadline. The result's
  byte count is admission size, not transmitted bytes.
- On rejection, do not add a UI pending record. `RejectedFull`,
  `RejectedInvalid`, and `RejectedClosed` are presentation/retry decisions, not
  physical write failures.
- File progress advances only from matching terminal success byte counts.
- `NativeSerialSendController` and `NativeSerialIoState` remain the owners of
  ManualSend/FileSend/Modbus arbitration. The transport session owns bytes,
  native I/O, cancellation request, and settlement.

## Execution Alerts

1. **Do not use the broad facade in migrated files.** Task 04 should bind to
   `SerialByteStream` and `SerialWriteScheduler`; `serialTransport_` remains
   only for the not-yet-migrated Modbus/RTU path until Task 05.
2. **Direct cancellation results are not requeued.** Ignoring the vector from
   `cancelPendingWrites()` leaks UI pending identities even though backend
   accounting is released.
3. **Current UI queue state drops active and byte accounting.** Copying only
   `pendingCount` cannot satisfy the task's active-work and dual-budget tests.
4. **Zero-byte read is not disconnect.** COM timeout and `cbInQue` races make an
   empty successful typed read a normal polling outcome.
5. **Stale cleanup and stale presentation are different.** Consume and remove
   the exact old pending pair once, but suppress every user-visible effect when
   its generation is no longer current.
6. **No new native cancellation mechanism.** The plan preserves synchronous
   Win32 I/O; `CancelIoEx` remains uncalled until a separately planned
   overlapped backend exists.

## Research Inputs and Sources

Project inputs:

- `.workflow/serial-transport-layer-v2/context/domain-knowledge.md`
- `.workflow/serial-transport-layer-v2/deepwiki-cache/phase-2-research.md`
- `.workflow/serial-transport-layer-v2/phases/phase-2/tasks/task-04-migrate-main-window-io.md`
- `src/transport/serial_session.h`
- `src/transport/serial_types.h`
- `src/transport/serial_write_queue.h/.cpp`
- `src/win32/win32_serial_session.h/.cpp`
- `src/win32/main_window.h`
- `src/win32/main_window_serial_io.cpp`
- `src/win32/main_window_send.cpp`
- `src/win32/native_serial_io_state.h/.cpp`
- `tests/native_serial_io_state_tests.cpp`

Exact declarations were cross-checked in:

- `/usr/share/mingw-w64/include/fileapi.h`
- `/usr/share/mingw-w64/include/commapi.h`
- `/usr/share/mingw-w64/include/winbase.h`
- `/usr/share/mingw-w64/include/synchapi.h`
- `/usr/share/mingw-w64/include/ioapiset.h`

Authoritative API pages used for fallback correction:

- `https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-readfile`
- `https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-writefile`
- `https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-clearcommerror`
- `https://learn.microsoft.com/windows/win32/api/winbase/ns-winbase-comstat`
- `https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject`
- `https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-setevent`
- `https://learn.microsoft.com/windows/win32/api/ioapiset/nf-ioapiset-cancelioex`
