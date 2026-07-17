# Phase 3 Task 02 API Reference

## Research Record

The original dedicated DeepWiki calls for Task 02 were interrupted. This file is
therefore a cache+official-source fallback based on `phase-3-research.md`, the
cached Task 01 API reference, and the Task 02 specification. No live network
query was used. Where generic guidance differs from checked-in project behavior,
the project tests, scripts, and typed result contract remain authoritative.

## Synchronous Win32 Contract

Official references:

- `WriteFile`: https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-writefile
- `SetCommTimeouts`: https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setcommtimeouts
- `CancelSynchronousIo`: https://learn.microsoft.com/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio
- `WaitForSingleObject`: https://learn.microsoft.com/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
- `PurgeComm`: https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-purgecomm
- `GetLastError`: https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror

Relevant signatures:

```cpp
BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
BOOL SetCommTimeouts(HANDLE, LPCOMMTIMEOUTS);
BOOL CancelSynchronousIo(HANDLE thread);
DWORD WaitForSingleObject(HANDLE, DWORD milliseconds);
BOOL PurgeComm(HANDLE, DWORD flags);
DWORD GetLastError();
```

### `WriteFile`

- Task 02 uses a synchronous communications handle and passes `nullptr` for
  `lpOverlapped`; the call itself returns the original operation's terminal
  success or failure.
- On `TRUE`, `lpNumberOfBytesWritten` is the completed byte count. A short
  successful write is not a full `Sent` result and must retain its partial count.
- On `FALSE`, capture `GetLastError()` immediately on the issuing worker thread.
  Cancellation intent, close intent, or a deadline does not replace that result.
- The handle, source buffer, request context, and worker state must stay alive
  until `WriteFile` actually returns, even after logical timeout publication.

### `SetCommTimeouts`

- The call replaces the `COMMTIMEOUTS` settings for the communications handle;
  zero indicates failure and requires an immediate `GetLastError()` capture.
- `WriteTotalTimeoutMultiplier` and `WriteTotalTimeoutConstant` define the
  configured total write timeout from byte count plus a constant. Both zero mean
  no total write timeout is configured.
- Driver timeout behavior is part of synchronous I/O, but it is not proof that
  cancellation settled, the worker exited, or resources can be released.
- Compute duration and deadline arithmetic with checked values and preserve the
  task's monotonic logical deadline independently of wall-clock changes.

### `CancelSynchronousIo`

- Cancellation is thread-targeted: the supplied thread handle identifies the
  thread currently issuing the synchronous `WriteFile`.
- `TRUE` means a cancellation request was made, not that `WriteFile` has settled.
- `FALSE` with `ERROR_NOT_FOUND` can be a completion race. It is not success,
  failure, or permission to release the buffer or COM handle.
- The original `WriteFile` may complete normally, report partial progress, fail
  with `ERROR_OPERATION_ABORTED`, or report another error. Task 02 must observe
  and retain that original terminal result.

### `WaitForSingleObject`

- Wait on the worker thread handle with a join budget capped at exactly `1000`
  milliseconds; do not use `INFINITE` or an unbounded retry loop.
- `WAIT_OBJECT_0` means the worker is signaled and can be joined/reaped before
  releasing its owned operation resources.
- `WAIT_TIMEOUT` means only that the 1000 ms wait expired. Keep the COM handle,
  buffer, operation context, worker, and waitable handles alive in quarantined
  ownership until the original synchronous operation later reaches terminal state.
- `WAIT_FAILED` requires immediate `GetLastError()` capture. `WAIT_ABANDONED` is
  meaningful for mutexes and is not a valid thread-join success result.
- Closing a handle while a wait on it is pending is undefined; lifetime ordering
  must make the wait finish before the waited handle is released.

### `PurgeComm`

- `PURGE_TXABORT` and `PURGE_RXABORT` terminate outstanding overlapped writes or
  reads; `PURGE_TXCLEAR` and `PURGE_RXCLEAR` discard queued driver-buffer bytes.
- `PurgeComm` returns immediately and can discard unsent data. It is not a flush,
  a synchronous-thread join, or evidence that the current `WriteFile` returned.
- Task 02 may preserve existing purge behavior where already required, but must
  not use it as a substitute for `CancelSynchronousIo` plus worker settlement.

### `GetLastError`

- Last-error state is per-thread and meaningful only when the preceding API's
  documented return value says to inspect it.
- Capture it at the failing API boundary before logging, formatting, locking, or
  making another Win32 call; later calls can overwrite the value.
- Stable typed status, category, native code, partial count, request ID, and
  generation drive control flow. Localized text is presentation only.

## Task 02 Settlement Rules

1. Assign every accepted request an ID, session generation, monotonic deadline,
   and one guarded terminal-publication slot.
2. Record timeout, caller-cancel, close, or disconnect intent before requesting
   native cancellation so racing outcomes map deterministically.
3. Call `CancelSynchronousIo` against the issuing worker, capture any failure,
   then bound the worker join with `WaitForSingleObject(..., 1000)`.
4. On a join timeout, a logical result may be published once through the guard,
   but native ownership remains quarantined; do not close or reuse any resource.
5. When the worker eventually settles, observe its original return, byte count,
   and native error for cleanup/evidence. It must not publish a second result.
6. Release queue pressure and request ownership exactly once. Native cleanup may
   occur later than logical publication, but only after native settlement.
7. Invalidate the old generation before close/reconnect publication. A stale
   result cannot mutate the replacement session, UI, counters, or queue.
8. Never replay an old request automatically after reconnect.
9. Keep all open, write, timeout, cancellation, purge, close, and handle-release
   operations under the session worker/owner's ordered lifetime policy.
10. Do not add `OVERLAPPED`, `GetOverlappedResult`, `CancelIoEx`, event-storage,
    or compatibility branches. Those belong to a future overlapped backend.

## CMake and CTest

- Register deterministic tests with `add_test(NAME ... COMMAND ...)` after
  testing is enabled. Build the configured tree before running CTest.
- Use `ctest --test-dir <tree> --output-on-failure -R <regex>` for focused
  lifecycle feedback and add `--no-tests=error` when an empty match must fail.
- A focused regex proves only its selected tests. Acceptance also requires the
  full unfiltered host and MinGW trees; host success does not prove Win32 code.
- CTest's nonzero exit is authoritative. Failure output is diagnostic evidence,
  not a replacement for checking the process result.

## Wine Boundary

- Use an isolated `WINEPREFIX`, initialize it with checked `wineboot -u`, and
  create the explicit lowercase COM symlink to the exclusively owned raw PTY.
- Run lifecycle and `normal,reopen,timeout,cancel,stress` evidence with the same
  prefix/mapping and propagate every nonzero Wine or harness exit.
- Wine proves this adapter through its Unix serial translation; it does not prove
  identical cancellation or unplug timing for physical vendor drivers.

## GitHub Actions Boundary

- Configure, build, CTest, self-test, UI-performance, PTY, package, and docs
  commands must preserve nonzero exits; check native-command status immediately.
- `GITHUB_STEP_SUMMARY` and uploaded logs are diagnostics, not pass/fail authority.
- Assert required artifacts exist, are non-empty, and contain the expected gate
  result before upload; retain `if-no-files-found: error` as an additional guard.
- The executable producing Wine/self-test evidence must be the same artifact that
  passed the relevant CTest and packaging gates.
