# API Reference - Task 02: Promote Layout Model Contract

Generated: 2026-07-06T17:17:27+08:00

DeepWiki status: succeeded. No fallback used.

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| `WM_SIZE` child layout trigger | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | High |
| `TabCtrl_AdjustRect` tab content bounds | Win32 API / Common Controls | DeepWiki: `microsoft/Windows-classic-samples` | High |
| `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` batched HWND layout | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | High |
| `ShowWindow` tab child visibility | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | High |
| `WM_DPICHANGED` / DPI layout reference | Win32 API | DeepWiki: `microsoft/Windows-classic-samples` | Medium |
| Pure layout model to child HWND application path | Project layout pattern informed by Win32 samples | DeepWiki + project inference | Medium-High |

## Win32 Resize and Tab Layout Notes

- `WM_SIZE` should be the single production entry point for recomputing child window bounds after client size changes.
- `TabCtrl_AdjustRect(hwnd, FALSE, &rect)` converts a tab control rectangle into its usable child display rectangle. In this project, deterministic model tests use a model-owned fallback rectangle, while production may isolate any HWND-specific refinement in one adapter point.
- `BeginDeferWindowPos` / `DeferWindowPos` / `EndDeferWindowPos` batch child HWND movement and reduce visible intermediate states during resize.
- `ShowWindow` return value reports previous visibility; a zero return is not a simple failure when showing a previously hidden child.
- DPI changes should alter model inputs or scaled metrics, not create a second geometry path.

## Contract Guidance

- Keep the dependency one-way: production calls `NativeLayoutModel`; tests target the model; the model must not call Win32 APIs.
- Model fields should cover tab bounds, tab page bounds, log bounds, prompt/helper bounds, split constraints, and visibility flags so future UI changes do not reopen manual geometry branches.
- The active tab path should update state and reuse the same layout application path used by resize.
- If HWND application fails, do not mutate the model to hide platform errors; keep failures observable through existing UI verification.
