# API Reference - Phase 4 Task 01: UI Capture Matrix

Generated: 2026-06-30T20:12:47+08:00

## Dependencies

| Library / API Area | Source | Confidence |
|---|---|---|
| GitHub Actions workflow evidence | DeepWiki failed for `actions/runner`; fallback to existing workflow plus official GitHub Actions behavior | Medium |
| Win32 desktop window capture | DeepWiki query against `microsoft/Windows-classic-samples` | Medium |

## Win32 Capture APIs

- `ShowWindow(HWND, SW_RESTORE)` restores a window before capture.
- `SetForegroundWindow(HWND)` improves input/click reliability before scripted interaction.
- `SetWindowPos(HWND, ..., cx, cy, SWP_NOZORDER | SWP_SHOWWINDOW)` applies deterministic capture sizes.
- Mouse input must be followed by short waits because tab switching and layout repaint are not instant.
- Screenshot validation should avoid fragile pixel-perfect assertions; use non-empty files, contrast checks, and broad region-detail checks.

## GitHub Actions Artifact Evidence

- Screenshot matrix files should use deterministic names so missing scenarios are obvious.
- `capture-status.txt` should contain one pass/fail-style line per scenario instead of a single `ok`.
- `$GITHUB_STEP_SUMMARY` should list both screenshots and status/log evidence.
- `actions/upload-artifact` should include `capture-status.txt`, `ui-perf-test.log`, and `window-info.txt` with `if-no-files-found: error`.

## Task 01 Action

Add explicit scenario status lines for default window, default tabs, compact tabs, resize sweep, and log splitter movement. Keep Wine capture an auxiliary smoke path and preserve existing screenshot visibility/detail checks.
