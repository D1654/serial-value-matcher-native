# API Reference — Phase 2 Task 03: Clarify Fake and PTY Stress Gates

Generated: 2026-07-07T23:48:32+08:00

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| artifact upload inputs/outputs | `actions/upload-artifact` | DeepWiki (`actions/upload-artifact`) | high |
| step summary file | GitHub Actions workflow commands | GitHub Docs | high |

## actions/upload-artifact artifact upload

**Source:** DeepWiki (`actions/upload-artifact`)

**Inputs used by this task:**
- `name`: artifact display name.
- `path`: required file/directory/glob list to upload.
- `if-no-files-found`: `warn`, `error`, or `ignore`; `error` makes missing evidence CI-blocking.
- `retention-days`: retention window. Repository limits may cap the value.
- `overwrite`: v4+ artifacts are immutable; duplicate names fail unless overwrite/delete behavior is used.

**Outputs:**
- `artifact-id`
- `artifact-url`
- `artifact-digest`

**Task implications:**
- Keep package evidence upload blocking with `if-no-files-found: error`.
- Make `serial-pty-matrix.txt` a required artifact file because it records the current local-only PTY status.
- Do not imply that the Windows package workflow executes PTY loopback scenarios; the workflow only creates and uploads the local-only evidence note.

**Gotchas:**
- v4+ artifacts are immutable; matrix jobs need unique names or explicit overwrite handling.
- Missing files can be intentionally non-blocking only with `warn` or `ignore`; package evidence should not use those modes.
- Hidden files are excluded unless explicitly included; this task does not rely on hidden evidence files.

## GitHub Actions step summary

**Source:** GitHub Docs, workflow commands for GitHub Actions.

**Usage:**
- Append Markdown to the file path in `$GITHUB_STEP_SUMMARY`.
- The summary is job-scoped and intended for human-readable workflow evidence.

**Task implications:**
- The package workflow summary should explicitly separate:
  - CI-blocking package gates.
  - CI-blocking evidence-file presence.
  - Local-only PTY release-candidate evidence.
- Summary wording must not claim that POSIX PTY loopback is executed on the Windows runner.

## Summary

Task 03 should preserve artifact uploads as CI-blocking package evidence while clearly labeling PTY loopback execution as local Linux/Wine release-candidate evidence. The workflow may upload a required text note, but that note is not equivalent to a CI-executed PTY loopback pass.
