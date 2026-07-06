# API Reference - Task 03: Harden Layout Transaction and Paint Policy

Generated: 2026-07-06T17:42:00+08:00

DeepWiki status: succeeded. No fallback used. Microsoft Learn was used as a cross-check, not as a fallback.

DeepWiki result: https://deepwiki.com/search/win32-api-reference-and-best-p_6a556b0a-8e4d-45a9-918c-895577557358

| API | Key Semantics | Project Guidance |
|-----|---------------|------------------|
| `WM_PAINT` + `BeginPaint` / `EndPaint` | Paint only inside `WM_PAINT`; every successful `BeginPaint` needs `EndPaint`. | Keep drawing in `paintLayoutChrome()`. Layout commits should trigger paint policy, not draw directly. |
| `WM_ERASEBKGND` | Returning nonzero means background erase is handled. | Suppress erase only during live resize or splitter drag; allow normal erase on settle/full refresh. |
| `MoveWindow` | Simple move/resize with repaint flag. | Use only for simple fallback paths; batch layout should not scatter `MoveWindow`. |
| `SetWindowPos` | Position, size, z-order, show/hide with flags such as `SWP_NOREDRAW`. | Use for direct fallback and z-order operations; live resize keeps `SWP_NOREDRAW`. |
| `BeginDeferWindowPos` | Starts a multi-window positioning batch; returns `HDWP` or `NULL`. | Preferred child HWND layout path for multiple moves. |
| `DeferWindowPos` | Appends a move to the batch; returns an updated `HDWP` or `NULL`. | Any failure abandons the batch and falls back to direct positioning. |
| `EndDeferWindowPos` | Applies the batch; success is nonzero. | On failure, mark stats and fall back without changing paint policy. |

## Implementation Guidance

- `WM_SIZE` and splitter drag should enter `NativeFrameScheduler`; frame consumption performs layout once and then calls one paint policy.
- A layout operation should have an explicit transaction: collect geometry, visibility, and z-order operations, commit once, then redraw once.
- `NativeLayoutTransaction` should guard duplicate commits. The first `commit()` applies and clears operations; later commits return prior stats and increment `duplicateCommits`.
- No late `move/show/showFast/moveTop` calls should enqueue after commit.
- Live resize should use no-erase/no-immediate redraw. Settled resize, first show, and full refresh can use erase and immediate paint.
