# API Reference - Task 04: Stabilize Workbench Split and Tab State

Generated: 2026-07-06T18:04:00+08:00

DeepWiki status: succeeded. No fallback used. Microsoft Learn was used as a cross-check, not as a fallback.

DeepWiki results:
- Tab child layout / `TCN_SELCHANGE`: https://deepwiki.com/search/for-win32-native-childwindow-l_b7397063-6674-4cff-892e-7961337e1086
- Splitter / resize / flicker: https://deepwiki.com/search/for-win32-splitter-or-resizabl_28b1866e-f955-4b90-90de-cc64723f4f90
- `PostMessage` / coalescing: https://deepwiki.com/search/for-win32-messageloop-scheduli_ff2b9e09-2910-459f-9d61-62b6deb6f560

| API / Pattern | Key Semantics | Project Guidance |
|----------------|---------------|------------------|
| `TCN_SELCHANGE` / `TabCtrl_GetCurSel` | Tab changes arrive through `WM_NOTIFY`; read the current tab before switching content. | User clicks and programmatic selection must converge on `applyWorkbenchTabVisibility`. |
| `ShowWindow` / `SetWindowPos(... SWP_SHOWWINDOW/SWP_HIDEWINDOW)` | Changes child HWND visibility; `ShowWindow` returns previous visibility. | Hide old tab controls and show target controls synchronously; never rely on hover/focus to reveal controls. |
| `SetWindowPos(HWND_TOP/HWND_BOTTOM)` | Controls z-order, position, size, and visibility. | Keep page background below tab controls and raise visible current-tab controls. |
| `TabCtrl_AdjustRect` | Calculates tab content area in Win32 samples. | Keep model-owned page bounds as the primary contract; any real-HWND tab adapter should be isolated. |
| `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` | Batches child HWND moves to reduce intermediate states and flicker. | Continue using `NativeLayoutTransaction` after split/layout changes. |
| `PostMessageW` | Asynchronous message queue; false means post failed. | Continue coalescing resize/split/tab/log/status in `NativeFrameScheduler`. |
| `WM_SETREDRAW` / `InvalidateRect` / `RedrawWindow` | Suppress redraw during batch updates, then invalidate/redraw once. | Restore redraw before final local refresh. Avoid unnecessary synchronous full-window repaint. |
| `EM_GETFIRSTVISIBLELINE` / `EM_LINESCROLL` / `EM_SCROLLCARET` | Save/restore log viewport or scroll to the latest content. | Follow latest only when the user was at bottom and log scroll is not paused/history-reading. |

## Implementation Guidance

- `NativeWorkbenchTabState` should treat layout revision, visibility readiness, page visibility, and normalized tab index as part of the applied state. A page becoming visible must reapply the current tab even when the layout revision is unchanged.
- Tab switching order should remain deterministic: normalize tab, create apply plan, update help/prompt, hide all or previous controls, show target controls, finish apply, then schedule repaint/raise.
- Split constraints belong in the layout model and metrics. `preferredWorkbenchHeight_` stores user intent; actual height must always be clamped by model constraints.
- Log scroll state should stay independent from layout transactions. Layout can record whether the log was at bottom before resizing and scroll back only when `NativeLogScrollState` says follow-latest is still valid.
