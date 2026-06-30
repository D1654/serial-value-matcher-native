# API Reference - Phase 3 Task 02: Log Evidence Regression

Generated: 2026-06-30T18:41:00+08:00

Source: DeepWiki query against `microsoft/Windows-classic-samples` for RichEdit visible-log messages and `WM_SETREDRAW`.

| API | Library | Source | Confidence |
|---|---|---|---|
| `EM_SETSEL` / `EM_REPLACESEL` | Win32 RichEdit | DeepWiki | High |
| `EM_EXLIMITTEXT` | Win32 RichEdit | DeepWiki | Medium |
| `EM_GETFIRSTVISIBLELINE` / `EM_LINESCROLL` / `EM_SCROLLCARET` | Win32 edit/RichEdit | DeepWiki | Medium |
| `EM_SETBKGNDCOLOR` / `EM_SETCHARFORMAT` | Win32 RichEdit | DeepWiki | High |
| `WM_SETREDRAW` | Win32 API | DeepWiki | Medium |
| `RedrawWindow` | Win32 API | DeepWiki + Phase 2 paint policy cache | Medium |

## RichEdit Append

**Messages:** `EM_SETSEL`, `EM_REPLACESEL`

**Usage here:**

- Move selection to the end of the RichEdit control.
- Replace the current selection with appended log text.
- Apply selection color with `EM_SETCHARFORMAT` before replacement when rich text coloring is active.

**Gotchas:**

- Repeated per-line append with redraw enabled is expensive.
- Batch adjacent lines of the same log kind where possible.

## RichEdit Limits

**Message:** `EM_EXLIMITTEXT`

**Usage here:**

- Cap the control-side text limit independently from the raw event store.

**Gotchas:**

- The RichEdit text limit is only a visible-control guard. It must not be treated as raw evidence retention.

## Scroll Preservation

**Messages:** `EM_GETFIRSTVISIBLELINE`, `EM_LINESCROLL`, `EM_SCROLLCARET`

**Usage here:**

- Detect first visible line before rebuild or append.
- Restore first visible line when the user is reading history.
- Scroll caret to bottom only when auto-follow is active.

**Gotchas:**

- Auto-follow and history-read modes must remain separate; appending evidence must not force-scroll while paused/history-reading.

## Formatting And Theme

**Messages:** `EM_SETBKGNDCOLOR`, `EM_SETCHARFORMAT`

**Usage here:**

- Apply log theme background and default foreground colors.
- Apply per-entry kind colors for RichEdit insertion.

**Gotchas:**

- Theme changes should invalidate/redraw the log control once, not force a rebuild of raw evidence.

## Redraw Control

**Messages/APIs:** `WM_SETREDRAW`, `RedrawWindow`

**Usage here:**

- Disable redraw during bulk insert/rebuild.
- Re-enable redraw and issue a single log-flush redraw through paint policy.

**Gotchas:**

- Always re-enable redraw after the guarded scope.
- Avoid synchronous redraw on every appended line; this is part of the Phase 2 hot-path performance contract.

## Task 02 Action

Task 02 should prove that raw I/O event persistence, visible-log trimming, filtering/search, and UI flushing are distinct. If gaps exist, add focused tests around visible trimming and raw evidence retention rather than expanding product behavior.
