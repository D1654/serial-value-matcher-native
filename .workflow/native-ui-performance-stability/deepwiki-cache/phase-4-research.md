# DeepWiki Cache - Phase 4: Engineering Matrix And Production Hardening

Generated: 2026-06-30T20:12:47+08:00

## Scope

Phase 4 turns the Windows native executable and its Actions artifacts into the authoritative engineering evidence set: UI capture matrix, DPI smoke, splitter drag frame gate, serial PTY edge matrix, package/security audit, and artifact summary integration.

## Sources

| Area | Source | Result |
|---|---|---|
| Win32 UI capture | DeepWiki query against `microsoft/Windows-classic-samples` | Covered window sizing, visibility, foregrounding, mouse interactions, and resize handling. |
| GitHub Actions evidence | DeepWiki query against `actions/runner`; fallback to official GitHub docs / action README knowledge | DeepWiki query failed; use existing workflow pattern, `$GITHUB_STEP_SUMMARY`, and `actions/upload-artifact` with `if-no-files-found: error`. |

## Win32 Capture Guidance

- Use `SetWindowPos` for deterministic size changes in resize/capture scenarios.
- Use `ShowWindow` / foreground activation before input-driven capture.
- Mouse-driven interactions should wait briefly after input to avoid racing layout/repaint.
- `PrintWindow` availability and fidelity vary; keep screenshot validation based on visible image existence, contrast, and bounded region detail.
- Resize scenarios should capture named outputs and verify final resized UI still has toolbar/sidebar detail.

## GitHub Actions Guidance

- Keep generated screenshots, logs, status text, and window metadata in one artifact directory.
- Append a markdown evidence list to `$GITHUB_STEP_SUMMARY` so a run can be reviewed without downloading first.
- Use `if-no-files-found: error` for screenshot/status artifacts so missing capture outputs fail the workflow.
- Keep Wine/Xvfb capture auxiliary; Windows runner PowerShell capture remains authoritative for final artifact evidence.
