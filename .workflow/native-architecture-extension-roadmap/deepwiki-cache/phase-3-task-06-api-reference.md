# API Reference — Phase 3 Task 06: Harden Package Docs Dependency Gates
Generated: 2026-07-09T20:31:51+08:00

## Scope

Task 06 dependency:

| Library | GitHub Repo | APIs Used | Usage |
|---------|-------------|-----------|-------|
| GitHub Actions artifact flow | `actions/upload-artifact` | artifact upload, missing-file behavior | Preserve package evidence and fail on missing artifacts. |

Research status:

- DeepWiki task-level `ask` queries for `actions/upload-artifact` succeeded before this file was written.
- Phase cache already covered the high-level direction: explicit paths, `if-no-files-found: error`, retention, immutable artifacts, unique names, hidden-file default, and digest outputs.
- Official fallback material used for version details: `actions/upload-artifact` `action.yml`, GitHub release metadata, tag refs, and the repository workflow under `.github/workflows/windows-native-package.yml`.
- A later official README fetch hit transient DNS/429 issues; conclusions below are therefore based on successful DeepWiki queries plus the official action metadata and local workflow semantics.

## Version and Action Pin

The current workflow uses:

```yaml
uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a
```

GitHub tag lookup showed this SHA is `actions/upload-artifact` `v7` / `v7.0.1`. The local comment already says `actions/upload-artifact v7`.

Important version notes:

- v7 keeps the v4-era artifact model that matters for this task: uploaded artifacts are immutable and duplicate artifact names fail unless `overwrite: true` is used.
- v7 `action.yml` runs on `node24`.
- Official release metadata for `v7.0.1` was published on 2026-04-10.
- DeepWiki notes no detailed "v7 behavior break" beyond the newer runtime/action version; v4/v7 guidance is aligned for this task's upload, missing-file, output, and retention gates.

## Path Matching and Upload Semantics

The `path` input accepts files, directories, wildcard patterns, and multi-line lists. It uses `@actions/glob` matching.

Key semantics:

- A directory path uploads files found under that directory; directories themselves are not artifact entries.
- Multi-line `path` values are combined into one upload set.
- Negated patterns starting with `!` exclude files from the upload set.
- Wildcards include `*`, `?`, `**`, and character classes such as `[abc]` or `[a-z]`.
- When a wildcard is used, the uploaded artifact preserves hierarchy after the first wildcard. The part before the first wildcard is effectively flattened away.
- With multiple paths, the artifact root is the least common ancestor of the search paths.
- For a single literal file path, the file's parent directory is the root.
- Tilde, environment variables, and GitHub expression values can be expanded by the action/workflow runtime.
- Hidden files are excluded by default in current v4+ action behavior unless `include-hidden-files: true` is set.
- File permissions are not preserved by the artifact service; uploaded files normally download with generic file modes. This does not affect Windows `.zip` release payload integrity because the package zip is itself one uploaded file.

Task 06 implication:

- The package evidence list should remain explicit and literal, not broad `artifacts/windows-native/**`, because this task is about proving specific release evidence exists.
- If a file is required, prefer a dedicated explicit path in `upload-artifact` plus a pre-upload assertion step for every required evidence file.

## Missing-File Behavior

`if-no-files-found` controls behavior only when the entire `path` search resolves to zero files:

| Value | Behavior |
|-------|----------|
| `warn` | Default. Emits a warning and the workflow continues. |
| `error` | Fails the action/workflow step. |
| `ignore` | Emits no warning/error and continues. |

Critical caveat:

- If a multi-line path list contains eight expected files and only one exists, `if-no-files-found: error` does not catch the seven missing files. The action uploads the one matched file because the total match count is non-zero.

Task 06 implication:

- Keep `if-no-files-found: error`, but do not rely on it as the only gate.
- Add or keep a pre-upload assertion that checks each required file individually. Required files in the current workflow are:
  - `${PACKAGE_NAME}.zip`
  - `${PACKAGE_NAME}.zip.sha256.txt`
  - `${PACKAGE_NAME}.package-summary.txt`
  - `native-ctest.log`
  - `native-self-test.log`
  - `native-ui-perf-test.log`
  - `phase-2-backend-regression.txt`
  - `serial-pty-matrix.txt`
- The existing workflow currently asserts only zip/hash/package summary before upload. Task 06 should extend that assertion to all required evidence logs so partial uploads cannot pass.

## Retention

`retention-days` controls how long the artifact is retained:

- `0` means use the repository default.
- Minimum is 1 day.
- Maximum is 90 days unless the repository settings allow a different maximum.
- The current workflow sets `retention-days: 14`, which is suitable for CI evidence on PR/push runs, but release documentation should not imply these Actions artifacts are permanent release storage.

Task 06 implication:

- Docs should distinguish short-lived GitHub Actions artifacts from permanent release assets, if release assets are used elsewhere.
- If the docs refer to "release evidence", include the retention window or avoid promising indefinite availability.

## Outputs

Current `actions/upload-artifact` exposes these outputs after a successful upload:

| Output | Meaning | Task 06 Use |
|--------|---------|-------------|
| `artifact-id` | GitHub artifact ID usable with the Actions Artifact REST API. | Can be written to job summary or manifest if later release evidence needs an API handle. |
| `artifact-url` | Authenticated GitHub URL for the artifact. Valid while artifact, run, and repository exist and the artifact has not expired/deleted. | Can be included in step summary or release notes for reviewers with repo access. |
| `artifact-digest` | SHA-256 digest for the uploaded artifact. | Can support audit logging of the exact uploaded artifact archive. |

Notes:

- `artifact-url` is not anonymous public download; users must be authenticated. A short-lived anonymous redirect can be generated through the REST API if needed.
- `artifact-digest` is the digest of the uploaded artifact object/archive produced by the action, not a replacement for the package's own `.zip.sha256.txt` file. Keep the package hash file as the portable release integrity check.
- If outputs are used, give the upload step an `id`, for example `id: upload-native-artifact`, then reference `${{ steps.upload-native-artifact.outputs.artifact-digest }}`.

## v4/v7 Common Pitfalls

- Duplicate artifact names in the same workflow run/job fail in v4+ unless `overwrite: true` is set.
- `overwrite: true` deletes/recreates the artifact and yields a new artifact ID; use only intentionally.
- Matrix jobs must use unique artifact names or merge artifacts after upload.
- v4+ has an artifact count limit per job; DeepWiki reports 500 artifacts/job. The current single native package artifact is safely below this.
- Hidden dotfiles are excluded by default. This is desirable for release evidence unless there is a deliberate hidden file requirement.
- `compression-level` ranges 0-9, default 6. Large already-compressed payloads such as `.zip` files can upload faster with level 0 if performance becomes a problem.
- v7 adds/retains `archive`; default is `true`. With `archive: false`, only a single file can be uploaded and the file name becomes the artifact name. The current multi-file evidence bundle should keep default archived behavior.
- `upload-artifact@v4+` is not supported on GitHub Enterprise Server according to action documentation. This project runs on GitHub-hosted Actions, so the current pin is acceptable.

## Recommended Workflow Shape for This Task

The current upload list is already explicit and uses `if-no-files-found: error` with 14-day retention:

```yaml
with:
  name: ${{ env.PACKAGE_NAME }}
  path: |
    artifacts/windows-native/${{ env.PACKAGE_NAME }}.zip
    artifacts/windows-native/${{ env.PACKAGE_NAME }}.zip.sha256.txt
    artifacts/windows-native/${{ env.PACKAGE_NAME }}.package-summary.txt
    artifacts/windows-native/native-ctest.log
    artifacts/windows-native/native-self-test.log
    artifacts/windows-native/native-ui-perf-test.log
    artifacts/windows-native/phase-2-backend-regression.txt
    artifacts/windows-native/serial-pty-matrix.txt
  if-no-files-found: error
  retention-days: 14
```

Task 06 should harden the workflow by adding per-file existence checks before upload for every listed evidence file, because the action's missing-file gate only catches the zero-files case.

Recommended extra evidence checks:

- Verify the package summary contains `Gate status: passed`.
- Verify the `.zip.sha256.txt` file exists and corresponds to the zip filename.
- Verify CTest, self-test, UI perf, backend regression, and serial matrix files are present and non-empty.
- Keep the upload path list in sync with docs consistency checks so docs drift fails before release.

## Implementation Guidance

- Do not replace explicit upload paths with a broad glob.
- Do not set `include-hidden-files: true` for this task.
- Do not use `overwrite: true`; the workflow uploads one named artifact once.
- Keep `retention-days: 14` unless release policy changes.
- Consider assigning `id` to the upload step only if Task 06 also records `artifact-id`, `artifact-url`, or `artifact-digest` in the step summary/docs. It is not required to enforce missing-file behavior.
- The package's own SHA-256 sidecar remains the user-facing integrity artifact; `artifact-digest` is CI-service evidence for the uploaded artifact bundle.
