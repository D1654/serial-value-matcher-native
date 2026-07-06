# API Reference — Task 06: Extract Serial Send Log UI Boundaries
Generated: 2026-07-06T18:26:52+08:00

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| `WM_COMMAND` dispatch with `LOWORD(wParam)` command id and `HIWORD(wParam)` notification code | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_commands.cpp` | High |
| Dispatch-table pattern (`MSDI`/`CMDI`, message/command id -> handler function) | Win32 API pattern | DeepWiki: `microsoft/Windows-classic-samples` (`ipxchat`-style dispatcher) | Medium |
| `WindowProc` / instance `handleMessage` dispatch with default fallback | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; Task 05 API reference; local `main_window.cpp` | High |
| `DefWindowProcW` fallback for unhandled messages | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; Task 05 API reference | High |
| `GWLP_USERDATA` / `SetWindowLongPtrW` / `GetWindowLongPtrW` class-window binding | Win32 API | Task 05 API reference; local lifecycle shape | High |
| `SetTimer`, `KillTimer`, and `WM_TIMER` timer dispatch | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_messages.cpp` | High |
| Button/combo/edit notification codes: `BN_CLICKED`, `CBN_SELCHANGE`, `EN_CHANGE`, `EN_KILLFOCUS` | Win32 common control patterns | Local `main_window_commands.cpp`; Win32 command dispatch conventions | High |
| `SendMessageW` control queries/updates (`CB_*`, `BM_*`, `PBM_*`) | Win32 API | Local `main_window_serial.cpp`, `main_window_send.cpp`, `main_window_log.cpp` | High |
| Pure state helper pattern (`NativeSerialIoState`, `NativeSendControlState`, `NativeLogFilterState`) | Project native state helpers | Local state headers/tests | High |

Research status: the required DeepWiki primary query against `microsoft/Windows-classic-samples` succeeded. No simplified retry or fallback was used.

## Command Dispatch

DeepWiki identifies two compatible Win32 command-dispatch shapes in Windows classic samples:

- A direct `WindowProc` / `HandleMessage` switch that handles `WM_COMMAND`, `WM_TIMER`, and other messages, then falls back to the default window procedure.
- A table-driven dispatcher where message ids or command ids map to handler functions and unrecognized entries flow to the default procedure.

The project already uses the direct shape with narrow classification:

- `NativeMainWindow::handleMessage` recognizes `WM_COMMAND` and calls `handleCommandMessage`.
- `handleCommandMessage` extracts `commandId = LOWORD(wParam)` and `notificationCode = HIWORD(wParam)`.
- Command ids are classified into quick, control, menu, and unknown domains.
- Control commands are then split into serial, log, send, file, and analysis handlers.
- Each handler returns `std::optional<LRESULT>` so unknown ids can fall through cleanly.

Task 06 should preserve that top-level dispatch. The controller-style helper should sit behind existing command handlers, not replace `WM_COMMAND` routing, command-id classification, or default-message behavior. Keep notification-code checks at the command boundary unless a helper receives the notification code as plain data and returns a decision.

## Controller Helper Boundary

Use the controller helper for serial/send/log decisions, not Win32 ownership. A conservative boundary is:

- Inputs: plain values, existing state references, payload-build results, booleans such as `serialOpen`, `manualSendAllowed`, requested timer period, command intent, and existing model state.
- Outputs: decision structs such as allow/deny, owner to acquire/release, normalized timer period, status text id/text, whether history should be saved, whether log/filter rebuild is needed, and whether a UI refresh is required.
- Side effects retained by `NativeMainWindow`: `SendMessageW`, `SetWindowTextW`, `SetTimer`, `KillTimer`, `OpenClipboard`, file dialogs, storage calls, serial-port reads/writes, raw-event persistence, and status-control updates.

Allowed helper responsibilities:

- Decide whether manual send is blocked by connection state or `NativeSerialIoState`.
- Normalize and decide timed-send timer state using `NativeSendControlState`.
- Keep quick-send validation and history selection rules testable without HWNDs.
- Decide log filter/search state transitions through `NativeLogFilterState`.
- Centralize repeated serial/send/log status decisions currently spread across `main_window_serial.cpp`, `main_window_send.cpp`, and `main_window_log.cpp`.

Avoid a broad "mini main window" helper. Do not pass every control handle or every `NativeMainWindow` member into the helper. If the helper becomes stateful, it should store only narrow state references or plain state, never `HWND`, `HMENU`, `HINSTANCE`, `HFONT`, worker handles, or ownership of timers.

## HWND Ownership

`NativeMainWindow` remains the owner of:

- The top-level `window_`.
- All child controls such as serial controls, send controls, history combo, log edit/rich edit, and status controls.
- Menu/resources, UI font/module lifetime, and shell context.
- Window creation, destruction, message loop, and process quit coordination.

The helper may receive borrowed data derived from HWNDs, but should not store or operate on HWNDs. Keep these operations in `NativeMainWindow`:

- `CreateWindowExW`, `DestroyWindow`, `PostQuitMessage`, menu creation, and child control creation.
- Control reads/writes through `SendMessageW`, `SetWindowTextW`, and helper wrappers such as `controlText`, `setControlText`, `selectedComboData`, and `enableControl`.
- Owner-window use for modal UI (`GetOpenFileNameW`, `GetSaveFileNameW`, clipboard owner window).
- Rich edit/log rendering calls such as `nativeLogInsertText`, `nativeLogSetSelection`, and scroll restoration.

This preserves the existing UI-thread and lifetime model while still moving non-UI decisions into testable code.

## Timers/UI Thread

The existing timer model is shell-owned:

- `IDT_SERIAL_POLL`: `pollSerial()`.
- `IDT_TIMED_SEND`: `sendPayload()`.
- `IDT_FILE_SEND`: `pumpFileSend()`.
- `IDT_RECONNECT`: `tryAutoReconnect()`.
- `IDT_LOG_FILTER`: debounce, then `updateLogFilter()`.
- `IDT_LOG_FLUSH`: end current timer, clear `logFlushTimerActive_`, then schedule a frame flush.
- `IDT_UI_PREFERENCES_SAVE` and `IDT_STATUS_CLOCK`: shell/status maintenance.

Task 06 should keep `SetTimer` and `KillTimer` calls on the UI thread in `NativeMainWindow`. A helper can return timer decisions, matching `NativeSendControlState::timerDecision`, but the caller should perform the Win32 timer call.

Important timer gotchas:

- `WM_TIMER` runs on the owning window's UI thread; do not move HWND access or timer callbacks into worker code.
- `KillTimer(window_, IDT_TIMED_SEND)` is part of connection close and timed-send refresh behavior; preserve it before recalculating timed-send state.
- Log filtering is intentionally debounced at `180ms`; do not rebuild the log on every edit notification.
- Log flushing is batched; `logFlushTimerActive_`, `pendingLogLines_`, and `scheduleLogFlushFrame()` are part of the current low-flicker/high-volume log path.

## Serial/Send/Log-Specific Gotchas

Serial ownership:

- `NativeSerialIoState` is exclusive for manual send, file send, and Modbus scan.
- Manual send blocks serial polling until the short write completes.
- File send owns writes but keeps serial polling allowed.
- Modbus scan blocks manual send, file send, polling, and line control, and it defers disconnect.
- Any extracted manual-send flow must release `NativeSerialIoOwner::ManualSend` on every write path.

Manual send:

- `sendPayloadFromText` currently validates connection, IO availability, payload parse errors, and empty payload before acquiring manual-send ownership.
- Serial write remains synchronous through `serialPort_.writeBytes(payload)`.
- On write failure, current behavior sets status, calls `handleSerialFailure`, and does not append TX log/history.
- On write success, current order is raw event save, optional history save/refresh, payload log append, TX byte counter update, status segment update, and final sent status.
- Quick send uses the same send path with `saveHistory = false`.

Timed send:

- Timed send should run only when timed send is enabled, the serial port is open, and manual send is currently allowed.
- Period normalization must remain centralized through `nativeNormalizeTimedSendPeriodMs`.
- Timer refresh is tied to connect, close, timed-send checkbox, timed period edit changes, and file-send stop/resume behavior.

Log model:

- `clearLog` cancels pending flush, clears pending visible lines, clears entries and filter state, resets scroll content, and updates status.
- `rebuildLogView` preserves first visible line when not following latest, resets search state, and consumes filter state.
- `findNextLogMatch` flushes pending log entries before reading visible text.
- Pause/follow behavior is in `NativeLogScrollState`; do not bypass hidden-line accounting.
- Log rendering sanitizes control characters and clips long rendered lines.

Command notifications:

- `IDC_DTR_CHECK` and `IDC_RTS_CHECK` should respond only to `BN_CLICKED`.
- Combo preference changes should remain on `CBN_SELCHANGE`.
- Log filter changes should use `EN_CHANGE` debounce.
- Timed period edits should update the timer on `EN_CHANGE` only when timed send is enabled, and save preferences on `EN_KILLFOCUS`.

## Project-Specific Guidance

Suggested Task 06 extraction shape:

- Create `native_serial_send_controller.h/.cpp` as a Win32-adjacent but HWND-free helper.
- Start with decision structs and small methods/functions rather than a broad object graph.
- Let `NativeMainWindow` collect control values and perform side effects; let the helper decide whether an action is allowed and which state transition/status should occur.
- Preserve `NativeMainWindow::handleCommandMessage`, `handleControlCommand`, `handleSerialControlCommand`, `handleSendControlCommand`, and `handleLogControlCommand` as the Win32 command boundary.
- Keep `main_window_serial.cpp`, `main_window_send.cpp`, and `main_window_log.cpp` as the owners of actual serial-port, storage, control, and log-view side effects during this task.
- Extend existing focused tests instead of adding UI tests for pure decision behavior: `native_serial_io_state_tests`, `native_send_control_state_tests`, `native_log_filter_state_tests`, and `native_send_history_state_tests`.
- If the helper needs status messages, prefer returning a semantic result or existing text id instead of calling `uiText` everywhere; this keeps tests stable and avoids mixing localization into core decisions unless already present locally.
- If a flow needs both a Win32 side effect and a pure state transition, split it into "prepare decision" in the helper and "apply decision" in `NativeMainWindow`.

Do not move in Task 06:

- HWND creation/destruction, top-level message loop, or `GWLP_USERDATA` binding.
- Timer ownership or `WM_TIMER` dispatch.
- Direct control mutation and queries.
- Clipboard/export/file dialog logic.
- Serial-port object ownership or worker-thread/lifetime coordination.
- Modbus scan ownership rules except where shared `NativeSerialIoState` decisions must remain compatible.

Validation focus:

- Behavior from the user's perspective must remain unchanged for connect/disconnect, manual send, quick send, timed send, send history, log filtering/search, pause/follow latest, and error status.
- Unknown command ids must still return `std::nullopt` from routing and fall through normally.
- Tests should prove the helper owns decisions, while `NativeMainWindow` still owns HWNDs and applies side effects on the UI thread.
