# API Reference - Phase 3 Task 05: Self-Test Expansion

Generated: 2026-06-30T19:44:49+08:00

## Sources

| API Area | Source | Confidence |
|---|---|---|
| Win32 window/control visibility and messages | DeepWiki query against `microsoft/Windows-classic-samples` | Medium |
| Repository self-test helpers | Local source | High |

## Win32 UI Self-Test Notes

- `CreateWindow` / `CreateWindowEx` create windows and controls after class registration; failures are represented by null `HWND`.
- `ShowWindow` changes visibility; `IsWindowVisible` checks whether a window and its parent chain are visible.
- `SendMessage` synchronously invokes the target window procedure and returns after the message is processed.
- `TabCtrl_SetCurSel` selects a tab programmatically but does not send `TCN_SELCHANGING` or `TCN_SELCHANGE`.
- Deterministic tab self-tests should therefore send the matching `WM_NOTIFY` / `NMHDR` notification after programmatic selection.
- Repaint assertions should avoid depending on asynchronous `WM_PAINT`; prefer deterministic state, visibility, geometry, layout counters, and bounded performance counters.

## Task 05 Action

Extend executable self-tests using existing local helpers and deterministic Win32 state probes. Avoid new repaint-pixel assertions unless they can be made synchronous and non-flaky.
