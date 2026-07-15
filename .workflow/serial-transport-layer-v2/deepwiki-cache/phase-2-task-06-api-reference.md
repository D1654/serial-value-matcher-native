# Phase 2 Task 06 API Reference

## Scope

Task 06 removes the temporary `SerialTransport` facade and leaves one concrete
Win32 session owner implementing only these contracts:

- `SerialSession`
- `SerialByteStream`
- `SerialWriteScheduler`

No compatibility header, alias, forwarding class, or queue capability base is
retained.

## Research Record

DeepWiki task-level queries were run against
`microsoft/Windows-classic-samples` for:

1. `CreateFileW` / `CloseHandle` ownership and borrowed-interface lifetime.
2. Synchronous `ReadFile` / `WriteFile` result handling and immediate
   `GetLastError` capture.
3. `SetEvent` -> `WaitForSingleObject` -> `CloseHandle` worker settlement.

DeepWiki did not contain a useful serial-specific `ClearCommError` example, so
the exact API behavior was checked against Microsoft Learn.

DeepWiki results:

- https://deepwiki.com/search/how-do-the-windows-classic-sam_23468bd4-c3f7-4f04-9fc9-7adbdd1e942b
- https://deepwiki.com/search/for-synchronous-serial-io-what_ca7326a7-b04f-4932-a848-f60acb6fac0c
- https://deepwiki.com/search/what-are-the-threadlifetime-ru_d74dfa5b-6dbf-4b99-8678-16f5d9a9ce09

Primary API references:

- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-clearcommerror
- https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
- https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setevent
- https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle

## API Findings

### `CreateFileW`

- A communications resource is an I/O device supported by `CreateFileW`.
- `GENERIC_READ | GENERIC_WRITE` requests the two capabilities needed by the
  serial session.
- A zero share mode prevents another open until the owning handle is closed.
- Devices use `OPEN_EXISTING`.
- Failure returns `INVALID_HANDLE_VALUE`; `GetLastError` must be captured
  immediately.
- A handle returned by `CreateFileW` must eventually be released with
  `CloseHandle` by the object that owns it.

Task application:

- `Win32SerialSession` remains the only COM-handle owner.
- Narrow interface references borrow the session object and never own or expose
  its native handle.
- Directly implementing the three narrow interfaces does not transfer handle
  ownership; the session destructor still settles workers and closes handles.

### `ReadFile`, `WriteFile`, and `ClearCommError`

- Synchronous `ReadFile` and `WriteFile` report success with a `BOOL` and the
  transferred byte count through their output parameter.
- A failed call requires immediate `GetLastError` capture before any other API
  call can overwrite thread-local error state.
- A successful short write is not a complete request. The owner must continue
  the suffix or publish a typed partial/failure result.
- A successful zero-byte serial read is not by itself a disconnect. The typed
  byte contract may report `Succeeded` with an empty buffer as a bounded poll.
- `ClearCommError` clears the device error condition and can return both line
  error flags and `COMSTAT`, including queued receive bytes.
- If a communications device is configured to abort on errors, new I/O is
  rejected until `ClearCommError` acknowledges the condition.

Task application:

- The facade removal must not move any native read/write/error policy into
  callers.
- `SerialByteStream` returns status, category, native code, generation, endpoint,
  deadline status, and byte count.
- `lastErrorText` is not a transport decision channel. Localized text remains a
  presentation concern.
- Native diagnostics may be computed transiently for internal handling, but no
  shared mutable localized-error field is retained.

### `SetEvent`, `WaitForSingleObject`, and `CloseHandle`

DeepWiki samples consistently use this worker-owner order:

1. Signal the worker stop event with `SetEvent`.
2. Wait for the thread handle with `WaitForSingleObject`.
3. Treat only `WAIT_OBJECT_0` as proof that the thread terminated.
4. Close the terminated thread handle with `CloseHandle`.
5. Close the event and device handles after no worker can use them.

Task application:

- `Win32SerialSession` owns its write thread and wake event.
- Session close settles queued and active writes, signals the worker, joins it,
  closes its thread handle, then closes the COM handle.
- Removing the facade does not change cancellation ordering or create a second
  handle owner.
- `SetEvent`, wait, and close failures must retain their native code in typed
  operation evidence where an operation result is published.

## Narrow C++ End State

`Win32SerialSession` should directly implement all three interfaces. This avoids
the redundant nested `CapabilityView` and makes `byteStream()` and
`writeScheduler()` return `*this`.

Required public typed surface:

```cpp
SerialOperationResult open(SerialOpenOptions options);
SerialOperationResult close();
SerialSessionSnapshot snapshot() const;
SerialOperationResult setDataTerminalReady(bool enabled);
SerialOperationResult setRequestToSend(bool enabled);
SerialByteStream& byteStream() noexcept;
SerialWriteScheduler& writeScheduler() noexcept;
SerialTerminalResult writeBytes(std::vector<std::uint8_t>, SerialDeadline);
SerialReadResult readAvailable(std::size_t, SerialDeadline);
SerialWriteAdmissionResult enqueueWrite(std::vector<std::uint8_t>, SerialDeadline);
std::vector<SerialTerminalResult> cancelPendingWrites();
std::vector<SerialTerminalResult> takeCompletedWrites();
SerialWriteQueueSnapshot writeQueueSnapshot() const;
```

Delete the legacy surface:

- bool/void lifecycle adapters
- endpoint/open-state text getters duplicated by `snapshot()`
- `lastErrorText`
- legacy `SerialIoResult`
- `waitForReadyRead`
- vector-only `readAvailable`
- optional-timeout queue adapter
- nested capability forwarding view
- legacy completion conversion

## Queue Boundary

`SerialWriteQueue` is a value object owned by the concrete session. It is not a
transport capability and must not inherit from an interface. Its public
`enqueue(...)` method remains the explicit internal queue operation. The
`SerialWriteScheduler` capability belongs to the session owner because only the
session can bind admission and completion to the active generation and endpoint.

## Test and Build Implications

- Contract fakes directly implement the three narrow interfaces and own a queue
  by composition.
- Native unit tests bind direct narrow references to `Win32SerialSession`.
- The loopback test uses typed open/write/read/close results and one absolute
  read deadline. Empty successful reads use a short sleep rather than a legacy
  ready-read/error-text probe.
- `tests/native_win32_serial_loopback_tests.cpp` is a required migration file
  even though it was omitted from the original task file list.
- `CMakeLists.txt` has no facade-header source entry and should remain unchanged
  unless a real reference is found.

## Structural Acceptance

- `src/transport/serial_transport.h` is absent.
- No active source or test references `SerialTransport`, `SerialWritePort`,
  `sessionCapability`, `SerialIoResult`, or `lastErrorText_`.
- `SerialWriteQueue` has no capability inheritance or override adapter.
- No compatibility type recreates the deleted facade.
- The complete portable and MinGW test trees remain green.
