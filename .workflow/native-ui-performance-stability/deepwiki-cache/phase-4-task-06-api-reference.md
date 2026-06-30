# API Reference - Phase 4 Task 06: Artifact Workflow Integration

Generated: 2026-06-30T21:24:44+08:00

## Sources

| API Area | Source | Confidence |
|---|---|---|
| GitHub Actions artifacts | Local workflow source and pinned `actions/upload-artifact` usage | High |
| Job summaries | Existing `$env:GITHUB_STEP_SUMMARY` workflow steps | High |

## Artifact Notes

- Package workflow should upload the release zip, SHA256 sidecar, package summary, native self-test log, UI performance log, and serial PTY matrix note/log when the PTY matrix is local-only.
- UI capture workflow should upload screenshots plus `capture-status.txt`, `window-info.txt`, and `ui-perf-test.log`.
- Workflow summaries should expose gate evidence without requiring users to download artifacts first.
- Missing release artifacts and failing package audit summary should remain hard failures.
