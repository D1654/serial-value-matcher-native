# DeepWiki Task Research - Phase 2, Task 01: Harden Win32 Session Owner

Generated: 2026-07-13

## Research Scope

This task is a behavior-preserving owner rename. It moves the existing
`Win32SerialPort` declaration and implementation to `Win32SerialSession`, makes
all direct references use the new name, and keeps the same single object as the
temporary `SerialTransport` implementation. It is not the task that implements
generation-aware settlement, the Phase 1 narrow capabilities, typed native
errors, new cancellation guarantees, or overlapped I/O.

Inputs reviewed:

- `.workflow/serial-transport-layer-v2/context/domain-knowledge.md`
- `.workflow/serial-transport-layer-v2/deepwiki-cache/phase-2-research.md`
- `.workflow/serial-transport-layer-v2/phases/phase-2/tasks/task-01-harden-win32-session-owner.md`
- `src/transport/serial_session.h`
- `src/transport/serial_types.h`
- `src/transport/serial_write_queue.h`
- `src/win32/win32_serial_port.h`
- `src/win32/win32_serial_port.cpp`
- current CMake, main-window, native serial, loopback-test, and architecture-document references

DeepWiki query completed successfully against
`microsoft/Windows-classic-samples`. It focused on `CreateFileW`/`CloseHandle`,
`SetupComm`/`PurgeComm`, DCB and timeout configuration, DTR/RTS control,
critical-section lifetime, event/thread creation, and thread waiting. The
repository samples provide useful generic Win32 ownership examples but only
limited serial-specific evidence. Normative edge cases still belong to the
Microsoft API contract; this task should avoid changing those edges.

## Existing Transitional Contract

The current backend implements `svm::transport::SerialTransport`, whose legacy
surface is still required by the main window, loopback test, and RTU adapter.
The Phase 1 narrow contracts already exist, but their signatures are different:

- `SerialSession`: structured `open`, `close`, `snapshot`, DTR/RTS operations,
  and borrowed `byteStream()`/`writeScheduler()` capabilities.
- `SerialByteStream`: structured write/read operations with explicit
  `SerialDeadline`.
- `SerialWriteScheduler`: structured admission, cancellation, completion, and
  queue snapshot operations.

Implementing those interfaces in Task 1 would require return-type and lifecycle
changes, not a rename. Therefore the minimum migration bridge for this task is
direct inheritance from the existing `SerialTransport` only. Do not add an
alias named `Win32SerialPort`, a forwarding wrapper, a second session object,
or a second native handle owner. Tasks 2-6 perform the semantic migration and
eventual facade removal.

## API-Specific Guidance

### COM handle open and close

- Preserve the current `CreateFileW` call exactly: normalized device path,
  `GENERIC_READ | GENERIC_WRITE`, share mode `0`, null security attributes,
  `OPEN_EXISTING`, `FILE_ATTRIBUTE_NORMAL`, and null template handle.
- The synchronous backend intentionally does not pass `FILE_FLAG_OVERLAPPED`.
- Treat only `INVALID_HANDLE_VALUE` as the `CreateFileW` failure return and
  capture `GetLastError()` immediately on that path.
- Keep the newly opened handle local until `SetupComm`, `PurgeComm`, DCB,
  timeouts, and control-line setup all succeed. On any setup failure, capture
  the failing native code before `CloseHandle(handle)` and do not publish the
  handle into the object.
- Preserve `close()` ordering: stop and join the write worker first, then close
  the COM handle and clear the queue. Do not move `CloseHandle` ahead of worker
  settlement in this rename.
- `CloseHandle` failure handling and richer native evidence are not currently
  observable behavior and belong to a later lifecycle task, not this rename.

### Queue setup and purge

- Preserve `SetupComm(handle, options.readBufferSize, 4096)` and its Boolean
  success test. Capture `GetLastError()` before closing the local COM handle on
  failure.
- Preserve the open-time `PurgeComm` flags:
  `PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR`.
- Do not add a close-time purge merely because it is a possible Win32 pattern.
  The existing backend does not do so, and it could change externally visible
  drain/discard behavior.

### DCB and COMMTIMEOUTS

- Preserve zero-initialization of `DCB`, assignment of `DCBlength`, and
  `GetCommState` before modifying fields.
- Preserve every current DCB field, especially `fBinary`, parity enablement,
  software XON/XOFF flags, CTS flow control, DTR mode, RTS handshake/manual
  mode, and `fAbortOnError = FALSE`.
- Preserve `SetCommState` before `SetCommTimeouts`.
- Preserve the exact timeout model: `ReadIntervalTimeout = MAXDWORD`, both
  multipliers zero, and total timeout constants sourced from the validated
  options. COM timeout semantics are non-trivial; Task 1 must not reinterpret a
  zero-byte read, polling timeout, or write timeout.
- These functions return zero on failure. Capture `GetLastError()` at the
  failing boundary before any cleanup call.

### DTR and RTS

- Preserve `EscapeCommFunction` commands `SETDTR`/`CLRDTR` and
  `SETRTS`/`CLRRTS`.
- Preserve the current open-time exception that tolerates
  `ERROR_NOT_SUPPORTED` only when disabling a line.
- Preserve the rule that manual RTS changes are rejected while hardware
  RTS/CTS flow control owns RTS. Do not change DCB flow-control policy here.
- Update cached options only after a successful manual line change.

### Critical section

- Preserve constructor-time `InitializeCriticalSection(&writeLock_)` and
  destructor-time `DeleteCriticalSection(&writeLock_)`.
- `InitializeCriticalSection` has no Boolean failure return, so do not add a
  `GetLastError()` branch around it.
- Delete the critical section only after `close()` has stopped/joined the write
  worker and after the wake event is no longer usable. No thread may enter or
  leave the critical section after deletion.
- Preserve the RAII `WriteLock` enter/leave helper and the current lock scopes;
  changing synchronization policy belongs to Task 2.

### Wake event and worker thread

- Preserve `CreateEventW(nullptr, FALSE, FALSE, nullptr)`: unnamed, auto-reset,
  initially nonsignaled. Its failure return is null; capture `GetLastError()`
  immediately.
- Preserve lazy event creation and ownership by the session. A later
  `CreateThread` failure leaves the event owned by the session for retry or
  destructor cleanup; do not introduce a second event owner.
- Preserve `CreateThread(nullptr, 0, threadProc, this, 0, nullptr)`: default
  security, default stack, immediate start, and the session pointer as context.
  Its failure return is null; capture `GetLastError()` immediately.
- Preserve shutdown signaling before waiting. Wait on the worker handle with
  `WaitForSingleObject(thread, INFINITE)`, then close the thread handle, clear
  worker state under the critical section, and only later close the wake event.
- `WaitForSingleObject` may return values other than `WAIT_OBJECT_0`, but the
  current code does not classify them. Adding wait-failure policy in Task 1
  would be a behavior change; record it for Task 2 rather than altering it here.
- The wake event remains valid for every worker wait/signal and is closed once
  by the session destructor after the worker is joined.

## Semantics That Must Not Change in Task 1

- Open begins by closing the current session and validating options.
- The COM handle is not published until all setup steps succeed.
- The endpoint is stored without the Win32 device prefix after successful open.
- `lastErrorText`, localized timeout-text classification, and legacy
  `SerialIoResult`/`SerialWriteResult` shapes remain unchanged temporarily.
- Synchronous `ReadFile`/`WriteFile`, polling through `ClearCommError`, current
  partial-write loop, and zero-byte-write failure behavior remain unchanged.
- Queue admission, pending cancellation, completion deque behavior, worker
  wakeups, and the current `writeInProgress_` accounting workaround remain
  unchanged. The dual-budget queue is not re-integrated in this rename.
- Close remains idempotent, stops the worker before closing the device, clears
  pending queue state, and preserves completed results as it does today.
- No generation is published, no active request is newly settled through the
  Phase 1 queue API, and no automatic replay or cancellation guarantee is
  introduced.
- Public methods expose no native `HANDLE`, event, critical section, internal
  queue, or buffer.
- The renamed type remains final, non-copyable, and non-movable; the destructor
  remains the shutdown owner.

## Task File-List Reconciliation

The planned create/delete/modify list covers the production owner and focused
test but is incomplete relative to the task's own structural verification.

Required additions to the Task 1 edit set:

- `tests/native_win32_serial_loopback_tests.cpp`: it includes
  `win32_serial_port.h` and constructs `Win32SerialPort` in three scenarios.
  Without updating it, the Win32 target graph cannot compile and
  `rg -n "win32_serial_port|Win32SerialPort" CMakeLists.txt src tests` cannot
  pass.
- `docs/Win32原生架构.md`: it explicitly documents
  `win32_serial_port.*` as the production adapter. Rename the documented owner
  to avoid shipping a stale architecture boundary.

No Task 1 change is needed in `docs/架构说明.md`; its generic `win32_serial`
module label and CreateFile/ReadFile/WriteFile description remain valid.

The main window must contain exactly one concrete object:
`Win32SerialSession serialSession_`. Its temporary
`SerialTransport& serialTransport_` reference may remain only if bound to that
same object. Renaming the member from `serialPort_` to `serialSession_` across
its current call sites is part of the owner rename, even though those `.cpp`
files need no include change because they include `main_window.h`. If a pure
member rename expands the diff substantially, retaining the private member name
`serialPort_` is behaviorally acceptable; the concrete type and single-owner
invariant are the required outcome.

## Verification Recommendations

- Update the native test to assert that `Win32SerialSession` still derives from
  `SerialTransport` during the bridge and is neither copyable nor movable.
- Run the planned native build and focused native/portable contract tests.
- Also ensure the loopback target compiles, even when its environment-dependent
  scenarios are skipped.
- Run the exact structural search across `CMakeLists.txt`, `src`, and `tests`;
  no old class or file name may remain.
- Search the new header for public `HANDLE` return types or accessors and verify
  only one `Win32SerialSession` data member exists in `NativeMainWindow`.

## Confidence

- **High**: task scope as a behavior-preserving rename; single-owner boundary;
  copy/move restrictions; event/thread/critical-section ownership order; actual
  repository references and required loopback-test supplement.
- **Medium-high**: current `CreateFileW`, setup, DCB, timeout, and DTR/RTS call
  sequencing, because it is directly observable in the existing implementation
  and DeepWiki confirmed the general success/failure patterns.
- **Medium**: serial-specific normative edge cases. DeepWiki's classic-samples
  evidence is sparse for DCB, COMMTIMEOUTS, `PurgeComm`, and driver-specific
  DTR/RTS behavior. The safe Task 1 response is to preserve current behavior
  exactly and defer semantic changes to later task-level research.
