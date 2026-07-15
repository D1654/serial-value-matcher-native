# Phase 2 Task 05 API Reference - RTU and Modbus Byte Borrowing

Generated: 2026-07-15

## Research Status and Fallback

The dedicated Task 05 research ran one task-specific DeepWiki `ask` query for
every Windows API listed in the task plan, all against
`microsoft/Windows-classic-samples`:

- `ReadFile`
- `WriteFile`
- `ClearCommError`
- `WaitForSingleObject`
- `CancelIoEx`
- `CloseHandle`

The `ReadFile`, `WriteFile`, and `WaitForSingleObject` queries returned useful
sample patterns and most generic contracts. The repository had no direct
`ClearCommError` or `CancelIoEx` reference, and its `CloseHandle` answer omitted
the exact native signature and debugger behavior. The required fallback chain
was therefore used:

1. DeepWiki `contents` completed for the repository, but did not add a precise
   serial contract for the missing APIs.
2. The exact contracts were verified against the official Microsoft Learn
   pages for all six APIs and `COMSTAT`.
3. C++20 library behavior was checked against the language/library contracts
   and the project's current types; no external repository is required.

DeepWiki sample evidence is useful for call ordering, but the Microsoft API
contract is authoritative where generated text was missing or imprecise. In
particular, `ERROR_IO_PENDING` is successful asynchronous admission rather than
terminal failure, `CancelIoEx` is only a cancellation request, and closing a
thread handle does not terminate the thread.

Confidence is **high** for signatures, ownership, typed byte-result handling,
generation checks, worker join ordering, and the current synchronous backend.
Confidence is **medium** for physical USB-to-serial cancellation and unplug
latency because drivers need not complete cancellation in a uniform time.

Official references:

- [ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile)
- [WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)
- [ClearCommError](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-clearcommerror)
- [COMSTAT](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-comstat)
- [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [CloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle)

## Existing Encapsulation and Ownership

| API | Current owner and use | Task 05 rule |
|---|---|---|
| `ReadFile` | `Win32SerialSession::readOperation`, after generation validation and `ClearCommError`; synchronous handle and `lpOverlapped == nullptr`. | `SerialRtuTransport` calls `SerialByteStream::readAvailable` and consumes an owning `SerialReadResult`; it never sees a `HANDLE`, `DWORD`, `COMSTAT`, or native buffer. |
| `WriteFile` | `Win32SerialSession::writeBytesToHandle`; synchronous partial-write loop behind the typed byte capability. | RTU calls typed `writeBytes`, validates the returned generation/status/count, and never implements a native partial-write loop. |
| `ClearCommError` | `Win32SerialSession` only; it obtains a receive-queue snapshot and communication-error mask under the session I/O lock. | RTU must not reproduce readiness polling. One typed bounded read replaces the old `waitForReadyRead` plus `readAvailable` facade sequence. |
| `WaitForSingleObject` | `Win32SerialSession` joins its serial write worker; `NativeMainWindow` separately joins the Modbus scan thread. | Serial waits stay in the session. Task 05 may wait only on the window-owned Modbus thread handle in the existing cancel/join path. |
| `CancelIoEx` | No call exists. The COM backend is synchronous and currently requests active cancellation with session-private `CancelSynchronousIo` plus `PurgeComm`. | Do not introduce `CancelIoEx` in RTU, worker, or window code. It remains a future overlapped-backend mechanism owned by the session. |
| `CloseHandle` | `Win32SerialSession` closes the COM handle and serial worker/event handles; `NativeMainWindow` closes only its Modbus thread handle. | RTU owns no handle. The window closes the Modbus thread handle only after confirmed termination; it never closes the serial handle. |

The capability relationship is non-owning:

- `SerialSession::byteStream()` returns a borrowed `SerialByteStream&` backed by
  the same `Win32SerialSession` that owns lifecycle, generation, locks, native
  handles, I/O buffers, and cancellation settlement.
- `SerialRtuTransport` may retain that reference only for the lifetime of one
  joined scan worker. It must not delete it, close it, downcast it, or cache any
  native address derived from it.
- The worker captures an immutable session generation and endpoint at scan
  admission. Endpoint metadata must be supplied separately; it is not a reason
  to borrow `SerialSession` or retain `SerialTransport` in the adapter.
- A narrow generation-current callback may query the lifecycle snapshot, but
  the adapter itself still stores only the byte capability plus immutable
  options. No concrete Win32 object crosses the worker boundary.

## Windows API Contracts

### `ReadFile`

```cpp
BOOL ReadFile(
    HANDLE hFile,
    LPVOID lpBuffer,
    DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead,
    LPOVERLAPPED lpOverlapped);
```

Parameters and lifetime:

- `hFile` is a file/device handle opened with read access. Asynchronous use
  requires `FILE_FLAG_OVERLAPPED`.
- `lpBuffer` points to writable caller storage. It must remain valid and must
  not be read, mutated, reallocated, or freed until the operation is terminal.
- `nNumberOfBytesToRead` is the maximum requested count.
- `lpNumberOfBytesRead` receives the synchronous count and is set to zero before
  work/error checking. It may be `NULL` only when `lpOverlapped` is non-null;
  overlapped callers should obtain the final count from completion. Windows 7
  required a non-null pointer.
- `lpOverlapped` must point to a valid, unique `OVERLAPPED` when the handle was
  opened with `FILE_FLAG_OVERLAPPED`; otherwise it can be null. The structure,
  event, and buffer must remain unchanged until final completion.

Returns and errors:

- Nonzero means the call succeeded. The reported byte count is authoritative
  and may be shorter than requested or zero.
- Zero means failure or asynchronous pending completion; call `GetLastError()`
  immediately.
- `ERROR_IO_PENDING` is not failure. Completion and the final byte count must be
  collected later.
- Cancelled I/O completes with `ERROR_OPERATION_ABORTED`. The API also documents
  resource failures such as `ERROR_INVALID_USER_BUFFER`,
  `ERROR_NOT_ENOUGH_MEMORY`, and `ERROR_NOT_ENOUGH_QUOTA` for excessive
  asynchronous work.
- Communications-device behavior is controlled by `COMMTIMEOUTS`. A successful
  zero-byte read is not proof of unplug or timeout by itself.
- Win32 does not promise one universal serial-unplug error code. The current
  session maps observed `ERROR_DEVICE_NOT_CONNECTED`, `ERROR_INVALID_HANDLE`,
  and `ERROR_GEN_FAILURE` to typed `Disconnected`; RTU must branch on the typed
  status/category, not reproduce numeric policy.

Task 05 application:

- `SerialByteStream::readAvailable(maxBytes, deadline)` returns owning bytes and
  a typed operation descriptor. RTU accepts bytes only when the terminal result
  belongs to the captured generation.
- `Succeeded` with an empty vector is a normal bounded poll outcome. Preserve
  the current deadline loop and short backoff without interpreting empty data as
  disconnect.
- A generation mismatch detected after the call makes the bytes stale. Discard
  them before response assembly and before the Rx frame callback.

### `WriteFile`

```cpp
BOOL WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped);
```

Parameters and lifetime:

- `hFile` is a file/device handle opened with write access; asynchronous use
  requires `FILE_FLAG_OVERLAPPED`.
- `lpBuffer` points to immutable bytes and must remain valid and unchanged until
  terminal completion.
- `nNumberOfBytesToWrite` is the requested count. Zero is a technology-specific
  null write; the typed serial contract should reject an empty RTU request
  before reaching native I/O.
- `lpNumberOfBytesWritten` receives the synchronous count and is set to zero
  before work/error checking. It may be null only with a non-null
  `lpOverlapped`; Windows 7 required it to be non-null.
- `lpOverlapped` follows the same handle-mode, uniqueness, and lifetime rules as
  `ReadFile`. Communications handles ignore file offsets.

Returns and errors:

- Nonzero means success; only the reported byte count was transferred.
- Zero means failure or asynchronous pending completion. Capture
  `GetLastError()` immediately.
- `ERROR_IO_PENDING` means an overlapped write was admitted and still requires
  terminal completion collection.
- Cancelled I/O completes with `ERROR_OPERATION_ABORTED`. Resource failures can
  include `ERROR_INVALID_USER_BUFFER`, `ERROR_NOT_ENOUGH_MEMORY`, and
  `ERROR_NOT_ENOUGH_QUOTA`.
- A successful short write is valid for some device types. The session must
  continue with the suffix or return a typed partial result; zero progress while
  bytes remain must not spin indefinitely.
- Communications write behavior is governed by `COMMTIMEOUTS`; the typed
  absolute deadline adds classification but does not promise exact interruption
  of a synchronous driver call.

Task 05 application:

- The session already owns the partial-write loop. RTU passes an owning vector
  to `SerialByteStream::writeBytes`, then requires terminal `Succeeded`, the
  captured generation, and a byte count equal to the request size.
- The Tx callback occurs only after those checks. A stale, cancelled,
  disconnected, failed, timeout, or partial typed result must never publish a
  valid request frame.

### `ClearCommError`

```cpp
BOOL ClearCommError(
    HANDLE hFile,
    LPDWORD lpErrors,
    LPCOMSTAT lpStat);
```

Parameters:

- `hFile` is the open communications-resource handle.
- `lpErrors` is optional output storage for a mask containing any combination
  of `CE_RXOVER`, `CE_OVERRUN`, `CE_RXPARITY`, `CE_FRAME`, and `CE_BREAK`.
- `lpStat` is optional output storage for a point-in-time `COMSTAT` value.

Relevant `COMSTAT` fields:

- `cbInQue` is the number of bytes received by the provider but not yet read by
  this process.
- `cbOutQue` is user data remaining for outstanding writes and is zero for a
  nonoverlapped write.
- `fCtsHold`, `fDsrHold`, `fRlsdHold`, and `fXoffHold` report output flow-control
  holds. Other bit fields report sent XOFF, EOF, and an immediate transmit
  character.

Returns and errors:

- Nonzero means the query/acknowledgement succeeded. A nonzero `*lpErrors` on
  this path is a reported communications condition, not a failed API call.
- Zero means failure; capture `GetLastError()` immediately.
- If `DCB::fAbortOnError` is true, a communications error terminates existing
  reads/writes and rejects new I/O until `ClearCommError` acknowledges it. The
  current session configures `fAbortOnError = FALSE`; Task 05 must not change
  that recovery policy.
- `COMSTAT` is only a snapshot. `cbInQue` may change before `ReadFile`, so a
  shorter or empty successful read is valid.

Task 05 application:

- `ClearCommError`, its error mask, and `COMSTAT` remain private to
  `Win32SerialSession`. RTU consumes only `SerialReadResult`.
- Removing the broad facade also removes the old two-call readiness race from
  RTU; do not add a replacement queue-status API to `SerialByteStream`.

### `WaitForSingleObject`

```cpp
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
```

Parameters:

- `hHandle` identifies a waitable object and must grant `SYNCHRONIZE`. The Task
  05 main-window use is the window-owned Modbus worker-thread handle.
- Closing the handle while a wait is pending has undefined behavior.
- `dwMilliseconds == 0` polls, a finite value bounds the wait, and `INFINITE`
  waits until the object becomes signaled. On Windows 8 and newer, finite waits
  do not count time spent in a low-power state.

Return values:

- `WAIT_OBJECT_0` (`0x00000000`) means the object is signaled. For a thread, its
  execution has terminated.
- `WAIT_TIMEOUT` (`0x00000102`) means a finite interval elapsed while the object
  remained nonsignaled.
- `WAIT_ABANDONED` (`0x00000080`) applies to an abandoned mutex and is not an
  expected thread result.
- `WAIT_FAILED` (`0xFFFFFFFF`) means failure; only this path supplies an extended
  wait error through `GetLastError()`.

Task 05 application:

- The current worker uses asynchronous `PostMessageW` and does not synchronously
  call back into the UI, so its thread can terminate while the UI waits. Do not
  introduce a worker dependency on UI message dispatch that would make the
  existing `INFINITE` join deadlock.
- Check for `WAIT_OBJECT_0` before treating the worker as joined. Thread
  termination settles execution, but the application must also retain or
  process the worker's terminal typed result before disconnect presentation and
  lifecycle close complete.
- Serial-session worker waits remain private to `Win32SerialSession`; Task 05
  does not expose them through the byte capability.

### `CancelIoEx`

```cpp
BOOL CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped);
```

Parameters:

- `hFile` identifies the file/device handle whose outstanding operations were
  issued by the current process.
- A null `lpOverlapped` marks all outstanding operations on that handle for
  cancellation, regardless of the issuing process thread.
- A non-null `lpOverlapped` matches operations issued for that handle with that
  exact `OVERLAPPED` object. The structure must remain alive and unreused.

Returns, races, and errors:

- Nonzero means the cancellation request was issued; it does not mean the I/O
  is terminal or that cancellation won the race.
- Zero means failure. `ERROR_NOT_FOUND` means no matching outstanding request
  was found and can be a benign race with normal completion.
- A marked operation may still complete normally, complete cancelled with
  `ERROR_OPERATION_ABORTED`, or fail with another error. The terminal status
  must be collected via `GetOverlappedResult`, a completion port/routine, or the
  owning backend's equivalent path.
- The buffer, `OVERLAPPED`, event, accounting reservation, and relevant handle
  must remain valid until terminal completion is observed. `CancelIoEx` neither
  waits nor changes the handle's state.

Task 05 application:

- The current COM handle is synchronous, so Task 05 must not call `CancelIoEx`.
  Active native cancellation remains session-private
  `CancelSynchronousIo(writeThread_)` plus `PurgeComm`; the RTU cancellation flag
  controls scan intent, not native handle cancellation.
- A future overlapped backend may use `CancelIoEx`, but it must preserve the
  same typed terminal-result contract. Neither RTU nor the Modbus worker gains a
  handle or `OVERLAPPED` in anticipation of that future.

### `CloseHandle`

```cpp
BOOL CloseHandle(HANDLE hObject);
```

Parameters and effects:

- `hObject` is a valid owned handle to an open kernel object. Supported objects
  include communications devices, files, events, threads, processes, mutexes,
  semaphores, pipes, and I/O completion ports.
- A successful close invalidates that handle value for the caller and
  decrements the object's handle count. The object is removed only after its
  lifetime conditions are met and the last handle is closed.
- Closing a thread handle does **not** terminate the thread. A running thread
  continues, so early close loses the caller's join handle and can expose
  application-owned context to a lifetime race.

Returns and errors:

- Nonzero means success. Zero means failure; capture `GetLastError()`.
- Under a debugger, passing an invalid or pseudo-handle can raise an exception;
  common causes include double-close and using `CloseHandle` where a specialized
  close function is required.
- Each owned handle is closed exactly once. Borrowed handles are never closed.

Task 05 application:

- `NativeMainWindow` requests scan cancellation, confirms the Modbus thread has
  terminated, consumes/retains its terminal result, and then closes only the
  Modbus thread handle.
- `Win32SerialSession` remains the sole closer of the COM handle and its serial
  worker/event handles. RTU and the scan worker own neither resource.

## C++20 Standard-Library Guidance

### `std::chrono`

- Use `std::chrono::steady_clock` for response and operation deadlines so wall
  clock changes cannot extend or shorten an RTU exchange.
- Build one absolute deadline from `milliseconds(max(0, timeout))`, then pass
  the remaining bound into typed operations. Avoid restarting a full timeout
  after each partial read.
- `system_clock` remains suitable only for persisted UTC timestamps; it is not
  a cancellation or generation clock.
- A deadline can classify a late synchronous result but cannot interrupt a
  driver at the exact instant it expires.

### `std::function`

- `std::function<bool()>` is suitable for the existing cancellation callback
  and a narrow generation-current guard; an empty function must be checked
  before invocation.
- The callable does not extend the lifetime of objects captured by reference or
  pointer. The main window/session must outlive the worker, enforced by the
  cancel-and-join path.
- Keep callbacks observational: cancellation/generation guards read state,
  timestamp callbacks create text, and frame callbacks enqueue evidence. They
  must not open, close, reconnect, or transfer handle ownership.
- Invoke the Tx/Rx frame callback only after the matching generation is
  revalidated, preserving the current Tx-before-Rx ordering.

### `std::vector`

- `std::vector<std::uint8_t>` owns contiguous RTU bytes. Moving a request into
  typed `writeBytes` transfers ownership without exposing a worker pointer to a
  native buffer.
- Reallocation, destruction, and many mutations invalidate `data()` pointers.
  The adapter must not retain a pointer across a typed operation or callback.
- `SerialReadResult::bytes` owns each received chunk. Append only after typed
  success and generation validation; discard the entire chunk when stale.
- Preserve current response accumulation, expected-length checks, and callback
  order. Task 05 adds no CRC, stale-RX purge, retry, inter-byte timing, or
  device-decoding policy.

### `std::atomic`

- The existing `std::atomic_bool` cancellation flag has one writer intent and
  worker polling. `load(std::memory_order_relaxed)` is sufficient while the flag
  communicates only that Boolean and does not publish other data.
- Atomic operations do not join the worker and do not make referenced session
  or window objects immortal. Cancellation must still be followed by terminal
  worker settlement and `WaitForSingleObject`.
- Do not make the captured generation mutable merely to use an atomic. It is an
  immutable scan identity; current generation is read through the synchronized
  session snapshot/guard.
- `std::atomic` objects are not copied into the worker context. Retaining the
  existing pointer is safe only because shutdown and disconnect join before the
  owning window is destroyed or the session is closed.

### `std::uint64_t`

- `SerialSessionGeneration` is an alias of `std::uint64_t`; zero is explicitly
  unassigned.
- Capture the nonzero generation from the accepted open snapshot immediately
  before creating the scan worker. Keep all 64 bits in context, worker results,
  progress/data messages, and comparisons.
- Do not cast generation to `DWORD`, `int`, timer IDs, control IDs, or text.
  Request IDs and session generations are different identity dimensions.

## Task 05 Generation and Result Rules

### Scan admission

1. Read one immutable `SerialSessionSnapshot` before acquiring/starting the
   Modbus worker. Require `open()` and a nonzero generation.
2. Put the borrowed `SerialByteStream*`, captured generation, captured endpoint,
   cancellation pointer, and narrow generation-current guard into the worker
   context. Do not pass `Win32SerialSession*`, `SerialTransport*`, or `HANDLE`.
3. If thread creation fails, release UI serial ownership immediately; no scan
   context or borrowed capability may survive.

### Before and after byte operations

1. Before every RTU exchange, reject cancellation or a non-current generation
   before writing any byte.
2. After typed write/read returns, require
   `result.operation.generation == capturedGeneration` before accepting its byte
   count or bytes. This post-call check closes the race where close/reopen occurs
   while synchronous I/O is active.
3. Re-check generation before every Tx/Rx frame callback and before adding bytes
   to response assembly. A stale chunk never becomes a valid frame.
4. Treat stale generation, `Cancelled`, and `RejectedClosed` as scan
   cancellation/disconnection, not a Modbus parse/protocol failure. Preserve a
   real typed `Disconnected` result as transport failure evidence for lifecycle
   handling, but never replay it against a replacement generation.

### Worker messages and terminal publication

- Every asynchronously posted progress batch, data batch, and terminal result
  must carry the captured generation, or be guarded immediately before posting
  and again by the UI on receipt. A pre-reconnect `PostMessageW` can remain in
  the queue after the generation changes, so worker-side checking alone is not
  a sufficient presentation barrier.
- The UI always settles the old worker/thread/ownership record, but it suppresses
  raw events, logs, progress, storage writes, counters, status, and serial-fault
  actions when the message generation is not the current scan generation.
- A stale terminal result cannot be reported as `completed`, `partial`, or a
  valid response frame for the replacement session. It is terminal internal
  cleanup classified as cancelled/disconnected.

## Cancel, Join, and Disconnect Settlement

1. A disconnect requested during a Modbus scan keeps the existing
   `disconnectAfterModbusScan_` policy: set the atomic cancellation request and
   defer typed serial close.
2. The worker observes cancellation/generation invalidation, finishes its
   terminal result exactly once, stops posting valid frames, and exits.
3. The UI receives or otherwise retains that terminal result, then waits for
   the Modbus thread to return `WAIT_OBJECT_0` and closes the thread handle once.
4. Only after worker termination may the UI release Modbus serial ownership and
   complete the deferred typed session close. This prevents the borrowed byte
   capability from outliving its owner or racing a replacement connection.
5. Shutdown follows the same request-cancel, settle, join, close-handle order.
   An atomic flag or successful cancellation request alone is not settlement.
6. `WAIT_FAILED` is not a successful join. Capture its native code and do not
   claim it is safe to destroy context or complete a replacement-session action
   until the worker lifetime is otherwise proven.

The worker must not synchronously call the UI while the UI joins it. Current
`PostMessageW` publication is nonblocking and preserves this requirement.

## Deferred Protocol Work

Task 05 preserves existing expected-length accumulation, timeout text, frame
callback order, and RTU bytes. Malformed-frame policy, late-frame policy,
stale-RX purging, CRC changes, retry changes, inter-byte timing, device-specific
decoding, and Gray-code interpretation remain later protocol/data-layer work.
They must not be introduced silently as part of the transport ownership
migration.

## Execution Alerts

1. Replace `SerialTransport&` in `SerialRtuTransport` with
   `SerialByteStream&`; do not add a forwarding facade or a handle member.
2. Preserve endpoint as immutable scan metadata rather than retrieving it from
   the byte capability.
3. Remove `waitForReadyRead` and localized `lastErrorText` decisions from RTU.
   Typed read/write status, category, native code, byte count, deadline, and
   generation are the only transport decisions.
4. Validate both before and after potentially blocking byte calls. A pre-call
   snapshot alone cannot prevent stale bytes after reconnect.
5. Do not drop a stale terminal message before internal settlement. Suppress its
   presentation while still joining the old thread and releasing only the old
   scan ownership.
6. `CancelIoEx` is not needed by the current synchronous backend and must not be
   added for architectural symmetry.
7. `CloseHandle(modbusScanThread_)` remains legitimate window-owned thread
   cleanup; any COM-handle `CloseHandle` outside `Win32SerialSession` is a
   boundary violation.
