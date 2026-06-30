# API Reference - Phase 4 Task 02: DPI Smoke Matrix

Generated: 2026-06-30T20:21:33+08:00

## Sources

| API Area | Source | Confidence |
|---|---|---|
| Win32 DPI metrics | DeepWiki query against `microsoft/Windows-classic-samples` failed with HTTP 500; fallback to Microsoft Learn | Medium |
| GitHub Actions workflow environment | Existing workflow and Phase 4 research cache | Medium |

## Win32 DPI Metrics

- `GetDpiForWindow(HWND)` returns the DPI value associated with the target window.
- `GetDC(nullptr)` plus `GetDeviceCaps(LOGPIXELSX / LOGPIXELSY)` records the screen logical DPI.
- `ReleaseDC(nullptr, hdc)` must be called after `GetDC`.
- 96 DPI represents 100%; 120 DPI represents 125%.

## Runner Limitation

Changing the Windows system display scaling inside a GitHub-hosted runner is not a reliable hard gate. Task 02 therefore records actual runner/window DPI metrics and adds deterministic 100% / 125% layout smoke captures by resizing the native window to scaled capture envelopes.

## Task 02 Action

Record DPI/window metrics in `window-info.txt`, add `dpi-100-window.png` and `dpi-125-window.png`, add `PASS dpi-smoke-100` / `PASS dpi-smoke-125` status lines, and add executable self-test layout probes that approximate 100% and 125% scale constraints.
