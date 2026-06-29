# UI Hot-Path Ownership Audit

Generated: 2026-06-30T07:16:50+08:00

## Scope

This audit maps the current Win32 native UI hot paths before Phase 2 introduces
layout, transaction, scheduler, and paint-policy boundaries. It does not change
product code.

Evidence used:

- Phase 2 DeepWiki cache:
  `.workflow/native-ui-performance-stability/deepwiki-cache/phase-2-research.md`
- Task 01 API cache:
  `.workflow/native-ui-performance-stability/deepwiki-cache/phase-2-task-01-api-reference.md`
- Source files under `src/win32`.

## Hot-Path Source Inventory

| Trigger | Current Files And Functions | Current Behavior | Classification |
|---|---|---|---|
| `WM_SIZE` | `src/win32/main_window.cpp:32`, `NativeMainWindow::layoutControls`, `RedrawWindow` | Calls `layoutControls(LOWORD, HIWORD)` directly, then full parent `RedrawWindow(... RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW)`. | Synchronous and erase-heavy. Correct trigger, but currently owns layout and repaint directly. |
| Splitter hover/cursor | `src/win32/main_window.cpp:39`, `splitterHitTest` | Uses hit-test state to set `IDC_SIZENS`. | Safe. Read-only state path. |
| Splitter double click | `src/win32/main_window.cpp:51`, `relayoutCurrentClient()`, `saveUiPreferences()` | Resets preferred workbench height, immediately relayouts, persists preference. | Safe as a settle action; immediate redraw is acceptable outside live drag. |
| Splitter drag start | `src/win32/main_window.cpp:62` | Sets `draggingWorkbenchSplitter_`, captures mouse, records drag start state. | Safe state capture. |
| `WM_MOUSEMOVE` splitter drag | `src/win32/main_window.cpp:77`, `clampedWorkbenchHeightForClient`, `relayoutCurrentClient(false)` | Computes latest requested workbench height and synchronously relayouts when height changes. | Continuous but still synchronous per mouse event. It avoids quantization but still couples state, layout, HWND mutation, and repaint. |
| Splitter drag layout | `src/win32/main_window_layout.cpp:69`, `layoutControls`, `WM_SETREDRAW`, partial `RedrawWindow(... RDW_NOERASE)` | Disables parent redraw while applying layout, includes old/new log/tab/page/splitter rectangles, then invalidates combined region without erase. | Good local mitigation. Needs formal paint-policy ownership and scheduler coalescing. |
| Splitter drag end/capture loss | `src/win32/main_window.cpp:94`, `WM_CAPTURECHANGED` | Releases drag state, relayouts immediately, saves preferences. | Safe settle path. |
| Main layout calculation and mutation | `src/win32/main_window_layout.cpp:150`, `layoutControls` | Calculates metrics, splitter rect, log/workbench/status geometry, tab page geometry, visibility flags, and immediately calls `moveControl`, `moveTopControl`, `showControl`, and `applyWorkbenchTabVisibility`. | Central risk. Pure geometry, state updates, HWND mutation, tab visibility, and repaint scheduling are in one monolithic hot path. |
| HWND move/show helpers | `src/win32/native_control_utils.cpp:39`, `showControl`; `:50`, `showControlFast`; `:81`, `moveControl`; `:93`, `moveTopControl` | Diffs geometry before `MoveWindow`; `showControlFast` uses `SetWindowPos(... SWP_NOREDRAW)`; `moveTopControl` can invalidate changed controls. | Helpful local diffing, but still per-control mutation. No batched transaction boundary yet. |
| Tab switch | `src/win32/main_window_messages.cpp:46`, `updateWorkbenchTab`, `applyWorkbenchTabVisibility` | `TCN_SELCHANGE` directly applies tab visibility. `NativeWorkbenchTabState` prevents some redundant work and `scheduleWorkbenchTabRepaint` posts a repaint message. | Partially scheduled. Visibility state is separate, but HWND show/hide and repaint policy are still inside workbench path. |
| Tab repaint | `src/win32/main_window_workbench.cpp:180`, `repaintWorkbenchTabControls` | Uses posted `kNativeWorkbenchTabRepaintMessage`; during drag uses `RDW_NOERASE`, outside drag uses `RDW_ERASE | RDW_UPDATENOW`. Raises visible controls and redraws workbench region. | Good scheduling seed. Needs unified paint-policy rules and transaction integration. |
| Side help update | `src/win32/main_window_workbench.cpp:296`, `updateSideHelp` | Avoids repeated help text updates and invalidates help controls when text changes. | Safe but repaint calls should later route through paint policy. |
| First show/first paint | `src/win32/main_window_lifecycle.cpp:20`, `show`; `main_window.cpp:36`, `paintLayoutChrome` | Window uses `WS_CLIPCHILDREN | WS_CLIPSIBLINGS`; initial show uses full erase/updatenow; paint draws splitter chrome. | Acceptable first-paint behavior. Keep explicit first-paint fallback. |
| Log append queue | `src/win32/main_window_log.cpp:143`, `queueVisibleLogText`, `scheduleLogFlush` | Queues rendered log text and starts a 40 ms flush timer if one is not active. | Good coalescing seed. |
| Log flush | `src/win32/main_window_messages.cpp:102`, `flushPendingLogEntries`; `native_log_view.cpp:72`, `NativeLogRedrawGuard` | Timer flush disables redraw on the log control, batches inserts by kind and size, restores scroll/selection, and redraws the log control with `RDW_ERASE | RDW_ALLCHILDREN`. | Good batching. Paint policy should control erase behavior and interaction with resize/splitter frames. |
| Log rebuild/filter | `src/win32/main_window_log.cpp:96`, `rebuildLogView`; `:306`, `updateLogFilter` | Rebuilds visible log with redraw suppressed, preserves scroll, trims visible data. | Safe but can be expensive; should remain outside layout transaction and be scheduled independently. |
| Status/progress | `src/win32/main_window_status.cpp:11`, `updateStatusSegments`; `native_progress_control.cpp:76` | Status text is cached before `SetWindowTextW`; progress control handles `WM_ERASEBKGND`, invalidates on size/range/position changes. | Mostly safe. Progress invalidation uses erase `TRUE`; future paint policy should decide whether that is necessary. |
| Shutdown | `src/win32/main_window_messages.cpp:115` | Kills timers, stops file/modbus/serial work, saves UI preferences. | Not a live UI hot path. |

## Current Safe Practices To Preserve

- `WS_CLIPCHILDREN | WS_CLIPSIBLINGS` on the main window.
- Geometry diffing before `MoveWindow` in `moveControl`.
- `showControlFast` using `SetWindowPos` with `SWP_NOREDRAW` for tab visibility.
- Splitter drag keeps continuous requested height instead of stepped buckets.
- Splitter drag uses `WM_SETREDRAW` and `RDW_NOERASE` partial redraw.
- Workbench tab repaint is posted through `kNativeWorkbenchTabRepaintMessage`.
- Log visible updates are timer-coalesced and wrapped by `NativeLogRedrawGuard`.
- Status segment text updates are cached before `SetWindowTextW`.

## Current Risks And Duplications

| Risk | Evidence | Impact |
|---|---|---|
| `WM_SIZE` owns full layout and synchronous erase-heavy repaint. | `main_window.cpp:32-35` | Resize can still flicker or stall because `RDW_UPDATENOW` forces immediate paint after every resize message. |
| Splitter drag is continuous but not frame-scheduled. | `main_window.cpp:77-90` | Every height-changing mouse move can run the full layout path. On fast input, stale intermediate layout work is not explicitly dropped. |
| `layoutControls` owns too many responsibilities. | `main_window_layout.cpp:150-631` | Geometry calculation, state mutation, HWND movement, visibility changes, tab application, and status positioning cannot be reviewed independently. |
| HWND mutation is diffed but not batched. | `native_control_utils.cpp:81-109` | Each changed control can still produce separate window-position work. Phase 2 should introduce a transaction boundary. |
| Repaint policy is embedded in callers. | `main_window.cpp`, `main_window_layout.cpp`, `main_window_workbench.cpp`, `native_log_view.cpp`, `native_progress_control.cpp` | It is hard to ensure hot paths consistently use no-erase/deferred repaint while settle paths use full refresh. |
| `WM_SETREDRAW` is manually scoped in multiple places. | `main_window_layout.cpp:82-91`, `native_log_view.cpp:72-83` | Useful but should be owned by a transaction/guard policy to prevent future unbalanced usage. |
| Tab visibility and layout revision are partially separated but still tied to layout. | `layoutControls` calls `applyWorkbenchTabVisibility`; `NativeWorkbenchTabState` handles revisions. | Later changes risk reintroducing blank/hidden tabs if layout and visibility rules diverge. |

## Ownership Rules

### State Ownership

- Event handlers may update input state only:
  splitter dragging state, requested workbench height, selected tab, pending log
  flags, and status/progress values.
- Event handlers must not be the long-term owner of HWND movement or repaint
  decisions.
- Future `NativeUiState` should own splitter, selected tab, pending frame
  reasons, visibility intent, and drag/settle state.

### Scheduling Ownership

- Future `NativeFrameScheduler` owns coalescing of `WM_SIZE`, `WM_MOUSEMOVE`
  splitter drag, tab switch, log flush, status/progress, and settle events.
- The scheduler must keep the latest target size/height/tab/log work and drop
  obsolete intermediate work.
- The scheduler must preserve continuous splitter position: it may coalesce
  stale work, but it must not quantize the selected height.

### Layout Ownership

- Future `NativeLayoutModel` owns pure geometry calculation.
- It should take window size, metrics, splitter/workbench state, active tab, and
  visibility constraints as input, then return a layout model without touching
  HWNDs.
- Existing `native_layout_metrics.*` is the seed for this model, but the current
  `layoutControls` body still contains too much mutation.

### HWND Mutation Ownership

- Future `NativeLayoutTransaction` owns all child HWND movement, show/hide, and
  z-order commits.
- It should diff geometry and visibility before mutating controls.
- It should use `BeginDeferWindowPos`/`DeferWindowPos`/`EndDeferWindowPos`
  where practical for coherent multi-control commits.
- `MoveWindow` with repaint `TRUE` should not be used in live drag/resize hot
  paths unless a control-specific requirement is documented.

### Paint Ownership

- Future `DirtyRegion` / `PaintPolicy` owns `RedrawWindow`, invalidation, erase,
  `UPDATENOW`, and `WM_SETREDRAW` rules.
- Hot interaction policy: prefer no-erase invalidation over full synchronous
  redraw.
- Settle/first-paint policy: allow explicit full refresh, including erase and
  `UPDATENOW`, only when the user interaction has ended or when first showing
  the window.
- Log control redraw, workbench tab redraw, progress invalidation, side-help
  invalidation, and splitter chrome invalidation should use named policies
  instead of raw flags at call sites.

## Before And After Responsibility Table

| Area | Before Phase 2 | After Phase 2 Target |
|---|---|---|
| `WM_SIZE` | Directly calls `layoutControls` and full synchronous `RedrawWindow`. | Updates latest size and schedules a layout frame. Paint policy decides redraw scope. |
| `WM_MOUSEMOVE` splitter drag | Computes height and synchronously relayouts per changed mouse move. | Updates latest splitter height and schedules/coalesces one frame while preserving continuous height. |
| Layout calculation | Mixed into `layoutControls` with HWND mutation and tab visibility. | `NativeLayoutModel` computes pure geometry and visibility intent. |
| Child HWND movement | `moveControl` and `moveTopControl` mutate one control at a time. | `NativeLayoutTransaction` diffs and batches all control moves/show/hide operations. |
| Tab switching | `TCN_SELCHANGE` applies visibility and schedules repaint through local workbench logic. | Tab state changes become scheduler reasons; transaction applies visibility; paint policy redraws only needed regions. |
| Log flush | Timer-coalesced but redraw flags are embedded in `NativeLogRedrawGuard`. | Log model keeps batching; paint policy owns erase/no-erase redraw behavior. |
| Status/progress | Cached text updates and custom progress invalidation live separately. | Status/progress changes become lightweight scheduler/paint reasons when they affect layout or visual regions. |
| First paint/settle | Full redraw flags appear in `show`, `WM_SIZE`, `relayoutCurrentClient`, and workbench repaint. | Full redraw is explicitly named as first-paint or settle policy, not used in live hot paths by default. |

## Phase 2 Implementation Guidance

1. Task 02 should extract enough of `layoutControls` into a pure
   `NativeLayoutModel` to test geometry without HWNDs.
2. Task 03 should route `moveControl`, `moveTopControl`, `showControl`, and
   `showControlFast` behavior through a transaction boundary.
3. Task 04 should introduce scheduler reasons for resize, splitter drag,
   splitter settle, tab switch, log flush, and status/progress.
4. Task 05 should replace raw hot-path `RedrawWindow` decisions with named paint
   policies and keep first-paint/settle fallbacks explicit.
5. Every code review after Phase 2 should reject new direct hot-path full
   `RDW_ERASE | RDW_UPDATENOW` calls unless they are inside the paint policy.

## Audit Conclusion

The current implementation already contains useful local mitigations, especially
geometry diffing, drag-time no-erase partial redraw, posted tab repaint, and
timer-coalesced log flushing. The remaining architectural issue is ownership:
`WM_SIZE`, splitter drag, `layoutControls`, workbench visibility, HWND movement,
and repaint policy still overlap. Phase 2 should not change user-visible UI
style; it should separate those responsibilities so later performance and
stability fixes become reviewable and mechanically testable.
