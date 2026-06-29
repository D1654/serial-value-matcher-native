# API Reference — Phase 2 Task 01: UI Hot-Path Ownership Audit

Generated: 2026-06-30T07:18:00+08:00

| API | Library | Source | Confidence |
|---|---|---|---|
| `WM_SIZE` | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | High |
| `WM_MOUSEMOVE` | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | High |
| `SetWindowPos` | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | High |
| `MoveWindow` | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | High |
| `WM_SETREDRAW` | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | Medium |
| `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | High |
| `RedrawWindow` / invalidation behavior | Win32 API | DeepWiki `microsoft/Windows-classic-samples` | Medium |

## `WM_SIZE`

**Signature:** window message handled by `WndProc(HWND, UINT, WPARAM, LPARAM)`.

**Parameters:**

- `wParam`: size type, such as restored, minimized, or maximized.
- `lParam`: client width in low word and client height in high word.

**Returns:** handlers normally return `0` after processing.

**Gotchas:**

- It is the correct resize trigger, but should schedule a coherent layout pass
  instead of repeatedly forcing full child redraws.
- Minimized size should be guarded so layout math does not operate on invalid
  or tiny dimensions.

## `WM_MOUSEMOVE`

**Signature:** window message handled by `WndProc(HWND, UINT, WPARAM, LPARAM)`.

**Parameters:**

- `wParam`: mouse buttons and modifier key state.
- `lParam`: mouse x/y coordinate.

**Returns:** depends on the window procedure; drag handlers usually consume when
tracking is active.

**Gotchas:**

- Splitter drag should update latest desired splitter position and schedule
  work. It should avoid applying every stale intermediate mouse move as a full
  synchronous layout/redraw operation.

## `SetWindowPos`

**Signature:** `BOOL SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)`.

**Parameters:**

- `hWnd`: target window.
- `hWndInsertAfter`: Z-order target or `HWND_*` constant.
- `X`, `Y`: target position.
- `cx`, `cy`: target size.
- `uFlags`: flags such as `SWP_NOZORDER`, `SWP_NOACTIVATE`, `SWP_SHOWWINDOW`,
  `SWP_NOMOVE`, `SWP_NOSIZE`, and `SWP_FRAMECHANGED`.

**Returns:** nonzero on success, zero on failure.

**Gotchas:**

- Prefer explicit flags in hot paths so activation and Z-order do not change
  accidentally.
- Use diffed layout before calling to avoid redundant child moves.

## `MoveWindow`

**Signature:** `BOOL MoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint)`.

**Parameters:**

- `hWnd`: target window.
- `X`, `Y`: target position.
- `nWidth`, `nHeight`: target size.
- `bRepaint`: whether to repaint immediately after move/resize.

**Returns:** nonzero on success, zero on failure.

**Gotchas:**

- `bRepaint=TRUE` can generate immediate paint work for every child move.
- In hot resize/drag paths, use delayed invalidation or batched transaction
  behavior unless a control explicitly requires immediate repaint.

## `WM_SETREDRAW`

**Signature:** `SendMessage(hwnd, WM_SETREDRAW, redrawEnabled, 0)`.

**Parameters:**

- `wParam`: `TRUE` enables redraw; `FALSE` disables redraw.
- `lParam`: unused, should be `0`.

**Returns:** message has no meaningful return value.

**Gotchas:**

- It can suppress intermediate redraws while applying a group of updates.
- After re-enabling redraw, a deliberate invalidation or redraw is required.
- It should be scoped tightly; leaving redraw disabled is a severe UI bug.

## Deferred Window Positioning

**Signatures:**

- `HDWP BeginDeferWindowPos(int nNumWindows)`
- `HDWP DeferWindowPos(HDWP, HWND, HWND, int, int, int, int, UINT)`
- `BOOL EndDeferWindowPos(HDWP)`

**Parameters:**

- `nNumWindows`: initial count for the deferred-position structure.
- `HDWP`: mutable handle returned by begin/defer.
- Window, position, size, Z-order, and flags mirror `SetWindowPos`.

**Returns:**

- `BeginDeferWindowPos`: non-null handle on success.
- `DeferWindowPos`: updated non-null handle on success.
- `EndDeferWindowPos`: nonzero on success.

**Gotchas:**

- If `BeginDeferWindowPos` or `DeferWindowPos` returns `NULL`, the transaction
  failed and must not be blindly completed.
- This is the preferred mechanism for committing multiple child HWND geometry
  changes coherently.

## Redraw And Invalidation

**Relevant behavior:** `RedrawWindow`, `InvalidateRect`, `UpdateWindow`, and
`WM_ERASEBKGND` decide when paint occurs and whether background erase happens.

**Gotchas:**

- Avoid erase-heavy or `UPDATENOW`-style redraw in live splitter/resize hot
  paths unless there is a measured reason.
- Returning handled behavior for background erase or invalidating with erase
  disabled can reduce flicker when the final paint covers the area.
- Parent chrome, splitter strip, workbench content, log view, and status areas
  need separate dirty-region policy.

