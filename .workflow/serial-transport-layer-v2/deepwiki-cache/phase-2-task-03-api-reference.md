# Phase 2 Task 03 API Reference - Main-Window Serial Lifecycle

Generated: 2026-07-14

## Research Status

The task-level DeepWiki research completed against
`microsoft/Windows-classic-samples` in two focused queries:

1. `SendMessageW`, `SetTimer`, and `KillTimer` for window command and timer
   ownership.
2. `EscapeCommFunction`, `GetCommState`, `SetCommState`,
   `SetCommTimeouts`, and `CloseHandle` for synchronous COM-port ownership.

The UI query returned usable signatures, timer replacement behavior, and the
`WM_TIMER` dispatch model. The serial query found COM-handle examples but no
direct sample coverage for the four communications-configuration APIs, so it
could not provide their exact contracts. Per the workflow fallback rule, those
details below are completed from `phase-2-research.md`, the current
`Win32SerialSession` implementation, the installed Windows-compatible headers,
and stable Win32 API semantics available to the model.

Confidence is **high** for the ownership boundary, signatures, return-value
checks, timer behavior, DCB initialization, and typed-result migration.
Confidence is **medium** for device-specific DTR/RTS effects: USB-to-serial
drivers may differ after a valid API call, so the UI must report only the
session's observed structured result.

## Ownership Decision

| API | Required owner | Task 03 rule |
|---|---|---|
| `SendMessageW` | Main-window UI | Keep control queries, checkbox rollback, combo selection, and command dispatch in the UI. It must never be used as a serial-session completion channel. |
| `SetTimer` | Main-window UI | Keep reconnect and presentation scheduling in the window message loop. Starting a timer does not change serial-session state. |
| `KillTimer` | Main-window UI | Keep timer cleanup with the window that created the timer. Killing a timer is not serial close or I/O cancellation. |
| `EscapeCommFunction` | `Win32SerialSession` only | The UI requests DTR/RTS through `SerialSession`; only the concrete session may issue native line-control commands. |
| `GetCommState` | `Win32SerialSession` only | Read the native DCB only during owner-controlled open/configuration. Do not expose the DCB or COM handle to the window. |
| `SetCommState` | `Win32SerialSession` only | Apply baud/framing/flow-control configuration inside the single handle lifecycle. UI code supplies typed `SerialOpenOptions` only. |
| `SetCommTimeouts` | `Win32SerialSession` only | Configure handle-wide native I/O timeouts inside the session. UI timers and operation deadlines are separate concepts. |
| `CloseHandle` | `Win32SerialSession` only for the COM handle | The window calls typed `close()` and waits for its settlement contract; it never closes, purges, or borrows the native COM handle. |

The division is strict: UI APIs remain UI-owned even when they initiate a
reconnect, while every API that can inspect, configure, signal, or invalidate
the COM handle remains private to the session owner.

## UI API Contracts

### `SendMessageW`

```cpp
LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
```

- `hWnd` identifies the receiving window or control.
- `Msg`, `wParam`, and `lParam` are interpreted according to the specific
  window message, such as `BM_GETCHECK`, `BM_SETCHECK`, or `CB_GETCURSEL`.
- The return value is message-specific. There is no universal success/failure
  value and no generic `GetLastError` rule for the call.
- Delivery is synchronous: the call does not return until the receiving window
  procedure has processed the message. The current control calls therefore
  remain on the UI thread; a serial worker must not synchronously drive these
  controls.

Migration constraint: retain `SendMessageW` for reading selected options,
updating controls, and rolling back a failed DTR/RTS checkbox. Interpret errors
using each control message's documented values, for example `CB_ERR`, rather
than treating an arbitrary `LRESULT` as a Win32 transport error.

### `SetTimer`

```cpp
UINT_PTR SetTimer(
    HWND hWnd,
    UINT_PTR nIDEvent,
    UINT uElapse,
    TIMERPROC lpTimerFunc);
```

- With the current non-null `window_` and `lpTimerFunc == nullptr`, expiration
  posts `WM_TIMER` for the window message loop; the window procedure owns the
  follow-up action.
- `nIDEvent` is the UI timer identifier. Calling `SetTimer` again with the same
  `hWnd` and identifier replaces and resets the existing timer.
- `uElapse` is an approximate message-timer interval in milliseconds, not a
  serial operation deadline and not a real-time guarantee.
- Success returns a nonzero timer identifier; failure returns zero. The API
  contract does not make a later `GetLastError` value suitable as transport
  evidence.

Migration constraint: `IDT_RECONNECT` remains UI policy. A zero return may be
handled as a UI scheduling failure, but it must not fault or close the session.
Repeated scheduling intentionally resets the reconnect delay.

### `KillTimer`

```cpp
BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent);
```

- `hWnd` and `uIDEvent` identify the timer created by `SetTimer`.
- A nonzero return means success; zero means failure.
- Killing the timer prevents future timer generation but does not remove an
  already-posted `WM_TIMER` message from the queue.
- As with `SetTimer`, timer cleanup failure is UI evidence, not serial native
  evidence.

Migration constraint: every reconnect timer handler must re-check the current
reconnect policy and typed session snapshot. This makes a late, already-posted
`WM_TIMER` harmless after connect, close, or shutdown. `KillTimer` remains
idempotent presentation cleanup and must not replace `SerialSession::close()`.

## Serial API Contracts

### `EscapeCommFunction`

```cpp
BOOL EscapeCommFunction(HANDLE hFile, DWORD dwFunc);
```

- `hFile` is the open communications-resource handle owned by the session.
- Task 03 uses `SETDTR`/`CLRDTR` and `SETRTS`/`CLRRTS`; the API also supports
  break and software-flow-control commands that are outside this task.
- A nonzero return means the command was accepted successfully. Zero means
  failure; capture `GetLastError()` immediately before any other Win32 call.
- Manual DTR control conflicts with `DTR_CONTROL_HANDSHAKE`. Manual RTS control
  conflicts with `RTS_CONTROL_HANDSHAKE`; the product's hardware RTS/CTS mode
  must therefore reject manual RTS before issuing the API call.
- A successful return applies the requested line-control command to the current
  handle. Driver-specific electrical timing is not a UI-owned guarantee.

Migration constraint: the window calls structured
`setDataTerminalReady(bool)` or `setRequestToSend(bool)`. Only a succeeded
result updates reconnect options and leaves the checkbox changed. Rejection or
failure rolls the checkbox back and localizes the typed status/category in the
UI; no decision may parse `lastErrorText`.

### `GetCommState`

```cpp
BOOL GetCommState(HANDLE hFile, LPDCB lpDCB);
```

- `hFile` is the valid COM handle.
- `lpDCB` points to caller-owned output storage. Zero-initialize the `DCB` and
  set `DCB::DCBlength = sizeof(DCB)` before the call.
- A nonzero return means the DCB snapshot was retrieved. Zero means failure;
  capture `GetLastError()` immediately.
- The returned DCB is a point-in-time value, not a live view and not a safe
  object to expose across the handle lifecycle.

Migration constraint: keep the current read-modify-write configuration pattern
inside `Win32SerialSession::open`. The main window carries immutable typed
options/snapshots and never reads a native DCB.

### `SetCommState`

```cpp
BOOL SetCommState(HANDLE hFile, LPDCB lpDCB);
```

- `hFile` is the valid COM handle and `lpDCB` supplies the complete desired
  control settings.
- A nonzero return means the settings were accepted. Zero means failure;
  capture `GetLastError()` immediately.
- On success, the communications device's hardware/control settings are
  reinitialized for the handle. The call does not empty the input or output
  queues and is not a close/drain operation.
- `fDtrControl` and `fRtsControl` decide whether line state is manually enabled,
  disabled, or driver-handshaken. `RTS_CONTROL_HANDSHAKE` is the product's
  hardware RTS/CTS boundary.

Migration constraint: apply baud rate, framing, parity, and flow-control modes
only while the session serializes open/configure. UI changes while closed are
next-open options; live DTR/RTS changes use the typed line-control operations
instead of rebuilding a DCB in the window.

### `SetCommTimeouts`

```cpp
BOOL SetCommTimeouts(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts);
```

- `hFile` is the valid COM handle.
- `lpCommTimeouts` points to the complete five-field `COMMTIMEOUTS` value:
  read interval, read multiplier, read constant, write multiplier, and write
  constant.
- A nonzero return means the timeout configuration was accepted. Zero means
  failure; capture `GetLastError()` immediately.
- The settings govern native reads and writes associated with the handle. They
  do not create UI timers and do not replace per-operation absolute deadlines.

Migration constraint: the session configures these values during open and
retains deadline classification in the typed operation layer. `SetTimer` for
reconnect/timed-send behavior must never be presented as a serial I/O timeout.

### `CloseHandle`

```cpp
BOOL CloseHandle(HANDLE hObject);
```

- `hObject` is the owned kernel handle. A nonzero return means success; zero
  means failure and permits immediate `GetLastError()` capture.
- Closing invalidates that handle for the caller and decrements the underlying
  object's handle count. Double-close or use-after-close is invalid; an invalid
  handle can raise a debugger exception.
- Closing a thread handle does not terminate the thread. Likewise, closing the
  COM handle is not a substitute for requesting cancellation, collecting the
  active terminal result, and joining the session's worker.

Migration constraint: Task 03 must call the session's synchronous typed
`close()` and trust its Task 02 settlement order. The window may update timers
and presentation only after close returns. It must snapshot the endpoint before
close because the closed session is allowed to publish an empty endpoint and
generation zero.

## Error and Presentation Rules

1. `GetLastError()` is read immediately only after a serial API reports
   failure. Do not read it after success or after intervening UI/log calls.
2. The session maps the numeric code into `SerialErrorCategory` and preserves
   the native code in `SerialOperationResult`.
3. Main-window control flow branches on `SerialOperationStatus`,
   `SerialErrorCategory`, state, and generation. Localized text is presentation
   only.
4. `SendMessageW` results are message-specific. `SetTimer`/`KillTimer` failures
   are UI scheduling/cleanup failures; none of these three APIs supplies serial
   session evidence.
5. A `Disconnected` line-control or open result belongs to the generation that
   produced it. It cannot mutate or replay work in a later generation.

## Task 03 Migration Constraints

- Bind lifecycle code to `serialSession_.sessionCapability()` and obtain state,
  endpoint, options, and generation through immutable `SerialSessionSnapshot`
  values. The temporary `serialTransport_` reference remains only for Task 04
  I/O call sites and must reference the same owner.
- Connect validates `SerialOpenOptions`, calls typed `open`, records the
  returned generation, then reads the successful snapshot for logs and profile
  persistence. Failed open results are localized without parsing diagnostic
  text for control flow.
- Disconnect and shutdown call typed `close()` exactly once. They do not call
  `CloseHandle`, `PurgeComm`, `EscapeCommFunction`, or any other serial native
  API. UI timers are killed and presentation state is updated after settlement.
- Reconnect policy remains in `NativeReconnectState` plus the UI timer. It
  copies the last successful `SerialOpenOptions`, replaces only the endpoint,
  calls typed `open`, and accepts the new generation. It carries no old request
  ID and performs no automatic replay.
- DTR/RTS checkbox selection while closed updates next-connection UI options.
  While open, typed line-control success updates both the session snapshot and
  reconnect options; rejection/failure rolls back the checkbox.
- Connection and disconnect logs, reconnect status, and raw-evidence metadata
  use an immutable endpoint snapshot. They must not call a concrete port getter
  after close or retain a mutable options reference.
- `currentOpenOptions()` remains responsible for user-selected next-open
  configuration. After a successful open, profile persistence should use the
  accepted typed options/snapshot rather than concrete backend internals.
- `SendMessageW`, `SetTimer`, and `KillTimer` stay visible in main-window source.
  The verification prohibition applies to native serial APIs, not ordinary UI
  message-loop APIs.

## Execution Dependency Alert

`tryAutoReconnect()` and `handleSerialFailure()` currently live in
`src/win32/main_window_serial_io.cpp`. Task 03 Step 5 requires reconnect
lifecycle migration, but that file is absent from Task 03's modify list and is
assigned to Task 04.

The execution must choose one explicit interpretation:

1. Include only these two lifecycle methods from
   `main_window_serial_io.cpp` in Task 03, leaving polling and queue operations
   untouched for Task 04; or
2. State that Task 03 intentionally leaves reconnect on the temporary facade
   and make Task 04 responsible for its final typed migration.

The first interpretation matches Task 03's objective and avoids a lifecycle
caller remaining on the broad facade. Merely passing the current Task 03 `rg`
checks would not prove reconnect migration because those checks omit
`main_window_serial_io.cpp`.

## Research Inputs

- `.workflow/serial-transport-layer-v2/context/project-brief.md`
- `.workflow/serial-transport-layer-v2/context/domain-knowledge.md`
- `.workflow/serial-transport-layer-v2/context/hypothesis-tracker.md`
- `.workflow/serial-transport-layer-v2/deepwiki-cache/phase-2-research.md`
- `.workflow/serial-transport-layer-v2/phases/phase-2/tasks/task-03-migrate-main-window-lifecycle.md`
- `src/win32/main_window*.cpp`, `src/win32/win32_serial_session.cpp`, and
  `src/transport/serial_session.h`
- Installed MinGW Windows headers for declaration verification
