# API Reference — Task 07: Extract Modbus Analysis Preference UI Boundaries
Generated: 2026-07-06T18:44:50+08:00

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| `WM_COMMAND` dispatch via `LOWORD(wParam)` command id and `HIWORD(wParam)` notification code | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_commands.cpp` | High |
| Direct `WndProc` / instance `handleMessage` switch with `DefWindowProcW` fallback | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; Task 05/06 API references; local `main_window.cpp` | High |
| Dispatch-table pattern (`MSD`/`MSDI`, `CMD`/`CMDI`, message/command id -> handler) | Win32 API pattern | DeepWiki: `microsoft/Windows-classic-samples` | Medium |
| `GWLP_USERDATA` class-window binding and borrowed shell context | Win32 API pattern | Task 05 API reference; local `native_main_window_context.h` | High |
| `SetTimer`, `KillTimer`, and `WM_TIMER` dispatch | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `main_window_messages.cpp`, `main_window_preferences.cpp` | High |
| `PostMessageW` worker-to-UI callbacks with `WM_APP + N` private messages | Win32 API | DeepWiki: `microsoft/Windows-classic-samples`; local `native_modbus_scan_worker.h/.cpp`, `main_window.cpp` | High |
| Heap payload ownership through `LPARAM` for posted worker results | Win32 API pattern | Local `native_modbus_scan_worker.cpp`, `main_window_modbus.cpp` | High |
| `SendMessageW` / `SetWindowTextW` child-control reads and updates | Win32 API | Local `main_window_modbus.cpp`, `main_window_analysis.cpp`, `main_window_preferences.cpp` | High |
| Owner-window modal UI (`MessageBoxW`, `GetSaveFileNameW`) | Win32 API | Local `main_window_modbus.cpp`, `main_window_analysis.cpp` | High |
| Pure state helpers (`NativeModbusScanUiState`, `native_ui_preferences`) | Project native helpers | Local state headers/tests | High |

Research status: the required DeepWiki primary query against `microsoft/Windows-classic-samples` succeeded. No simplified retry or fallback was used.

## Command/Message Dispatch

DeepWiki confirms two Win32-compatible dispatch shapes in Windows classic samples: a direct window-procedure switch and a table-driven message/command dispatcher. This project already uses the direct shape:

- `NativeMainWindow::handleMessage` switches on raw messages and falls back to `DefWindowProcW`.
- `WM_COMMAND` is routed through `handleCommandMessage`, then split into quick, control, menu, and unknown domains.
- Control commands are further split into serial, log, send, file, and analysis domains.
- Unknown command ids return `std::nullopt` and fall through normally.

Task 07 should preserve this shell-level routing. The new Modbus/analysis/preferences helper should sit behind `handleAnalysisControlCommand`, `handleAnalysisMenuCommand`, and preference-related callers; it should not replace the Win32 message switch or command-domain classifiers.

## Controller Helper Boundary

Use the Task 07 helper for feature decisions and state coordination, not Win32 ownership.

Good inputs:

- Plain booleans such as scan running, serial open, store open, serial IO available, target parse success, observation presence, and candidate presence.
- Plain values read by `NativeMainWindow`, such as slave id, function code, address range, target label/value/unit, tolerance, normalized preference values, and selected candidate id.
- Existing pure state references or values, such as `NativeModbusScanUiState`, `NativeCandidateCacheState`-derived facts, and `native_storage::UiPreferences` snapshots.

Good outputs:

- Decision structs: start scan, cancel scan, show invalid request, run analysis, save rule, run verification, export report, or no-op.
- Status/message intent, not direct `setStatus` calls unless the helper remains tightly UI-adjacent and test expectations justify it.
- UI snapshot decisions such as button mode, exclusive-controls enabled, timer restart needed, candidate refresh needed, or preferences-save needed.

Keep these side effects in `NativeMainWindow` during this task:

- `SendMessageW`, `SetWindowTextW`, `MessageBoxW`, `GetSaveFileNameW`, `MoveWindow`, and `GetWindowRect`.
- `CreateThread`, `PostMessageW` handling, `WaitForSingleObject`, `CloseHandle`, and worker cancellation.
- Serial-port reads/writes, storage persistence, log insertion, combo population, raw event counters, and direct timer calls.

## HWND Ownership

`NativeMainWindow` remains the owner of the top-level `HWND`, all child control `HWND`s, `HMENU`, `HINSTANCE`, `HFONT`, rich edit module lifetime, timers, and message loop. The controller helper should not store `HWND`, `HMENU`, `HINSTANCE`, `HFONT`, or worker handles.

If a helper needs UI facts, let `NativeMainWindow` read them first and pass plain data. If a helper needs to request UI changes, return a decision that `NativeMainWindow` applies on the UI thread. This keeps the Task 05 shell boundary intact and avoids rebuilding a smaller `NativeMainWindow` under a different name.

## Worker Callback Messages

The Modbus worker uses private UI-window messages:

- `kNativeModbusScanDoneMessage = WM_APP + 14`
- `kNativeModbusScanProgressMessage = WM_APP + 15`
- `kNativeModbusScanDataMessage = WM_APP + 16`

The worker allocates result/progress/batch payloads on the heap and posts them through `LPARAM`. If `PostMessageW` fails, the worker deletes the payload. UI handlers immediately wrap the pointer in `std::unique_ptr`, making the UI thread responsible for payload lifetime after a successful post.

Preserve this ownership rule. A helper may process already-owned values or return status/progress decisions, but it should not become the message target, own the posted pointer contract, or perform `PostMessageW` itself in Task 07.

Important local ordering:

1. `handleModbusScanDone` captures `disconnectAfterModbusScan_`.
2. It closes the worker thread handle.
3. It updates completed progress.
4. It persists the scan execution and appends the summary log.
5. It calls `setModbusScanRunningUi(false)` to release Modbus ownership and restore controls/timers.
6. It handles deferred disconnect or serial failure before final status.

## Preferences/Timer UI Thread

Timers are shell-owned and dispatched from `WM_TIMER` on the UI thread. Task 07 should keep `SetTimer` and `KillTimer` in `NativeMainWindow`.

Current timer coordination relevant to this task:

- `IDT_UI_PREFERENCES_SAVE`: debounced preference save. `scheduleUiPreferencesSave` resets a 300 ms timer and falls back to immediate save if timer creation fails.
- `IDT_SERIAL_POLL`: stopped while a Modbus scan owns serial IO, restarted when scan UI returns to idle and the serial port is open.
- `IDT_TIMED_SEND`: stopped while Modbus scan runs, recalculated through `updateTimedSendTimer` after scan completion.
- `kNativeMainWindowShellTimerIds`: all shell timers are killed during destroy before serial shutdown.

Preference persistence should remain UI-thread-owned because it reads HWND state, window geometry, combo selections, check boxes, quick-send edits, and current layout height. The pure preference helpers in `native_ui_preferences` should remain the normalization/comparison layer.

## Modbus/Analysis-Specific Gotchas

- `runModbusScan` has a toggle behavior: if Modbus already owns serial IO, the button requests cancellation instead of starting another scan.
- Starting a scan is gated by serial-open, store-open, and `NativeSerialIoState::allowsModbusScan`.
- Invalid scan input shows both status text and a `MessageBoxW` owned by the main window.
- `setModbusScanRunningUi(true)` stops serial poll and timed send, switches the Modbus button to stop mode, and disables exclusive controls including analysis/report/send controls.
- `setModbusScanRunningUi(false)` releases Modbus ownership, restarts serial poll if the port is open, and refreshes timed send.
- Progress/data/done worker callbacks must stay on the UI thread before touching controls, status counters, storage, or log state.
- Analysis depends on the latest persisted scan session and observations. Do not let the helper assume in-memory worker state is enough for `showAnalysisWorkspace`.
- `showAnalysisWorkspace` saves a match run, updates `candidateCacheState_`, refreshes the candidate combo, appends a system log line, and sets status. Preserve that visible order unless tests are updated intentionally.
- `showRuleVerification` may first save a rule from the selected candidate, then load the latest scan session and run verification.
- `exportReport` owns a save-file dialog with `dialog.hwndOwner = window_`; keep modal ownership in `NativeMainWindow`.
- Preference normalizers already define stable limits for timed send, file delay, log cache, raw retention, workbench height, window size, and quick-send slots. Do not duplicate those constants in the new helper.

## Project-Specific Guidance

Recommended Task 07 extraction shape:

- Create `native_modbus_analysis_controller.h/.cpp` as a Win32-adjacent but HWND-free helper.
- Start with small decision structs and free/static helper methods instead of a broad stateful object.
- Route analysis-domain commands through the existing `handleAnalysisControlCommand` and `handleAnalysisMenuCommand`; do not change command ids or domain classification.
- Let `NativeMainWindow` collect HWND values and apply side effects. Let the helper decide what action should happen from plain facts.
- Reuse `NativeModbusScanUiState::snapshot` for scan button/exclusive-control behavior rather than duplicating the running/idle rules.
- Reuse `nativeNormalizeUiPreferences` and `nativeUiPreferencesSameSettings` for preference state; avoid adding parallel normalization logic.
- Keep worker messages and timer calls in `NativeMainWindow`. A helper can return "restart serial poll", "refresh timed send", or "save preferences" intents, but the caller should perform the Win32 calls.
- Keep storage and serial executor behavior unchanged in this phase. This task prepares backend unification; it should not change Modbus executor semantics, retry/timeout behavior, persisted schema, or report content.
- Extend focused tests around pure decisions: `native_modbus_scan_ui_state_tests`, `native_analysis_report_tests`, and `native_ui_preferences_tests`. UI behavior should remain observable through existing native self-test/performance paths.

Validation focus:

- Unknown command ids still fall through to default handling.
- Modbus start/cancel, completion, deferred disconnect, and serial failure status behavior remain stable.
- Analysis/report flows still require persisted scan evidence and keep Chinese output/report text stable.
- Preference save debounce, minimized-window skip, window geometry normalization, and quick-send slot persistence remain unchanged.
