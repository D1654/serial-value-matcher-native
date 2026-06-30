# API Reference - Phase 4 Task 03: Splitter Drag Frame Gate

Generated: 2026-06-30T20:37:56+08:00

## Sources

| API Area | Source | Confidence |
|---|---|---|
| Win32 mouse/window movement | Phase 4 DeepWiki query against `microsoft/Windows-classic-samples` | Medium |
| Local frame scheduler and splitter logic | Repository source and tests | High |

## Win32 Drag Capture Notes

- Scripted drag evidence should use foreground activation, cursor movement, mouse down, intermediate mouse moves, mouse up, and short waits.
- Evidence should include intermediate frames, not only before/after screenshots.
- Status text should name the frame files and intended deltas so CI artifacts can be audited without replaying the script.

## Local Splitter Gate Notes

- Existing frame scheduler coalesces stale drag moves and applies the latest exact workbench height.
- Task 03 strengthens this by adding a self-test that processes two separate drag frames and checks both exact target heights.
- The gate must not quantize movement; small non-round deltas should remain exact unless clamped by normal layout constraints.
