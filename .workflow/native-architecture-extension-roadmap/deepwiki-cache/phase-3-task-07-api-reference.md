# API Reference - Phase 3 Task 07: Finalize Performance and Serial Evidence Gates
Generated: 2026-07-09T20:48:53+08:00

## Summary

Task 07 dependency research completed with DeepWiki `ask` queries. No network fallback was required.

The `actions/upload-artifact` findings are consistent with Phase 3 Task 06: explicit paths and `if-no-files-found: error` are necessary but not sufficient for a multi-file evidence bundle, because the missing-file gate only fails when zero files match. Task 07 should preserve Task 06's pre-upload per-file/content assertions and apply the same idea to UI capture evidence where wildcard screenshot upload can otherwise hide missing expected screenshots.

The Win32 evidence findings support a two-part gate: screenshots prove final visual states and basic resize/tab/splitter coverage, while `--ui-perf-test`, `capture-status.txt`, and `window-info.txt` provide the timing/message/layout context needed to interpret whether resize and UI work stayed responsive. Screenshots alone cannot prove smooth resize, repaint ordering, or message-loop responsiveness.

## API Table

| Library | GitHub Repo | APIs / Patterns Researched | Key Result | Usage in Task 07 |
|---------|-------------|----------------------------|------------|------------------|
| GitHub Actions artifact flow | `actions/upload-artifact` | `path`, `if-no-files-found`, `retention-days`, `artifact-id`, `artifact-url`, `artifact-digest`, upload summary behavior | Explicit multi-line paths are supported; `if-no-files-found: error` only catches zero matched files; outputs are available after upload; retention is bounded by repo policy; upload summary/log output does not replace project-specific `$GITHUB_STEP_SUMMARY` evidence. | Preserve package/UI evidence and record artifact metadata without treating upload success as proof every expected file exists. |
| Win32 API | `microsoft/Windows-classic-samples` | `WM_SIZE`, resize/layout/repaint patterns, lightweight message handling, screenshot evidence interpretation | Resize handling should be lightweight, update layout/render targets/child positions, and invalidate/repaint appropriately. Screenshots are single-frame evidence and must be correlated with timing/status logs. | Interpret UI perf, resize sweep, screenshot, tab-switch, DPI, and splitter evidence conservatively. |

## Research Status

- DeepWiki `ask actions/upload-artifact` succeeded.
- DeepWiki `ask microsoft/Windows-classic-samples` succeeded.
- Fallback path was not used. No `structure`/`contents` fallback or README/action.yml fallback was needed for this task.
- Local semantics reviewed read-only:
  - `.github/workflows/windows-native-package.yml`
  - `.github/workflows/windows-native-ui-capture.yml`
  - `scripts/capture-windows-native-ui.ps1`
  - `scripts/capture-windows-native-ui-wine.sh`
  - `scripts/run-windows-native-serial-pty-loopback.py`
  - `scripts/check-docs-artifact-consistency.py`
  - `docs/Windows原生UI验证.md`
  - `docs/Windows串口真机验收.md`

## actions/upload-artifact Conclusions

DeepWiki confirms these Task 06 conclusions remain valid for Task 07:

- `path` accepts explicit files, directories, globs, exclusions, and multi-line lists.
- `if-no-files-found: error` fails the upload step only when the resolved upload set is empty.
- If a multi-line list or wildcard set expects many files but at least one file matches, upload can still succeed with a partial evidence set.
- `retention-days` controls Actions artifact retention. `14` days is suitable for CI evidence, but docs must not describe these artifacts as permanent release storage.
- Successful uploads expose `artifact-id`, `artifact-url`, and `artifact-digest`.
- `artifact-digest` is CI artifact-object evidence. It does not replace the package zip's own SHA256 sidecar.
- v4+ artifact behavior is immutable by default; duplicate names fail unless `overwrite: true` is intentionally used.

Task 07-specific interpretation:

- Keep explicit package evidence paths in `windows-native-package.yml`.
- Keep `id: upload-native-artifact` and the explicit summary step that writes upload outputs to `$GITHUB_STEP_SUMMARY`.
- For UI capture, `artifacts/windows-native-ui/*.png` plus `if-no-files-found: error` proves at least one screenshot exists, not that all expected screenshots exist. Add or preserve a pre-upload gate that checks required files and required `capture-status.txt` PASS tokens.
- If UI artifact metadata is needed in job summary, assign an `id` to the UI upload step and record `artifact-id`, `artifact-url`, and `artifact-digest` the same way package upload does.
- Do not use `overwrite: true` for these single-upload evidence artifacts.
- Do not replace explicit evidence paths with broad artifact-directory upload unless a separate manifest/content assertion proves the expected evidence set.

## Step Summary and Outputs

DeepWiki reports that `upload-artifact` emits upload progress/success information and artifact details, and exposes outputs for downstream steps. For this project, reviewer-facing release evidence should still be workflow-authored:

- Use `$GITHUB_STEP_SUMMARY` for the project's gate status, evidence file list, baseline policy, serial boundary table, and artifact outputs.
- Treat the action's own upload summary as supporting service metadata, not as the project's evidence manifest.
- Keep package summary and UI capture summary deterministic so docs consistency can assert exact terms.

Existing local package workflow already follows this shape:

- Upload step has `id: upload-native-artifact`.
- A later summary step records `artifact-id`, `artifact-url`, and `artifact-digest`.
- Pre-upload assertions check package zip, SHA256 sidecar, package summary, CTest log, self-test log, UI perf log, backend regression note, and serial PTY matrix note.

## Win32 UI Evidence Conclusions

DeepWiki's Win32 guidance supports these review rules:

- `WM_SIZE` and resize handlers should remain lightweight. Heavy computation in message handlers blocks the UI message loop.
- Proper resize evidence should show layout recalculation and repaint/invalidation behavior, not just a resized outer window.
- Classic Win32 patterns commonly separate message receipt from an `OnResize`/layout handler and update child windows/render state promptly.
- Resize/move operations can require special repaint policy to avoid flicker and redundant paints.
- Screenshot evidence is limited to a single captured frame. It cannot prove resize smoothness, flicker absence, event ordering, or responsiveness during the resize interaction.
- Performance/status logs are needed to interpret screenshots: they can show whether scenarios ran, whether frames changed, and whether timing/layout/log flush metrics stayed inside the accepted baseline.

Task 07-specific interpretation:

- `capture-status.txt` should remain the scenario manifest for screenshot evidence.
- Required UI status tokens should include default window, tab set, compact tab set, resize sweep, DPI smoke on Windows capture, splitter drag frames, phase closure, and capture complete.
- `window-info.txt` should remain the DPI/window metrics source and should be uploaded with screenshots.
- Screenshot checks should keep blank/low-detail and frame-difference gates, because a screenshot file existing is weaker than a screenshot proving UI state.
- `--ui-perf-test` should be read as the timing/layout/log performance gate, with stable fields:
  - `ui-perf ok`
  - `tabs`
  - `tab-ms`
  - `layout-pass`
  - `apply`
  - `revision`
  - `log-lines`
  - `log-ms`
  - `log-flush`
  - `log-rebuild`

## Serial Evidence Conclusions

The current repository boundary is sound and should stay explicit:

- Native CTest serial/unit coverage is CI-blocking.
- `svm-native-win32.exe --self-test` and `--ui-perf-test` are CI-blocking in the package workflow.
- `serial-pty-matrix.txt` is a CI-blocking evidence file, but it records that the PTY normal/reopen/timeout/cancel/stress matrix is local-only release-candidate evidence.
- The Windows `windows-2022` runner does not execute the POSIX PTY/Wine dosdevices matrix in the current workflow.
- Local PTY output should remain release-candidate evidence when serial I/O, timeout, cancel, reconnect, async write queue, or batch sending behavior changes.

## Implementation Recommendations for Task 07

1. UI performance baseline:
   - Derive acceptance language from current release/artifact `--ui-perf-test` output.
   - Require `ui-perf ok` plus stable metric fields instead of introducing arbitrary new thresholds.
   - If thresholds are changed, record the baseline artifact/run that justified the change.

2. UI capture evidence:
   - Add a deterministic summary file only if it improves machine checks; otherwise keep `capture-status.txt`, `ui-perf-test.log`, and `window-info.txt` as the stable evidence trio.
   - Add pre-upload assertions in `windows-native-ui-capture.yml` for required files and required PASS tokens, because wildcard PNG upload can partially succeed.
   - Keep screenshot blank/detail/diff checks in both Windows and Wine capture paths.
   - Preserve the distinction that Wine screenshots are diagnostic smoke evidence, while GitHub Actions Windows artifact and real Windows screenshots carry final visual weight.

3. Package artifact evidence:
   - Keep Task 06's per-file assertions before `upload-artifact`.
   - Continue recording `artifact-id`, `artifact-url`, and `artifact-digest` in job summary.
   - Continue stating that `artifact-digest` is CI upload evidence, not a replacement for the zip SHA256 sidecar.

4. Serial evidence:
   - Keep fake/native serial tests CI-blocking.
   - Keep PTY loopback as local-only release-candidate evidence unless a real CI substitute is implemented.
   - Ensure `serial-pty-matrix.txt` includes the local command and expected scenarios: `normal,reopen,timeout,cancel,stress`.
   - Do not let docs imply that the Windows package workflow already executes the PTY matrix.

5. Docs consistency:
   - Extend `scripts/check-docs-artifact-consistency.py` only for stable terms that must remain synchronized across docs and workflows.
   - Candidate stable terms: `ui-perf ok`, `capture-status.txt`, `window-info.txt`, `serial-pty-matrix.txt`, `local-only release-candidate evidence`, `artifact-digest`, and `if-no-files-found: error`.
   - Avoid checking volatile timing numbers in docs consistency; those belong in artifact logs, not static docs.

## Caveats

- DeepWiki's `actions/upload-artifact` answer described the v4-era behavior model. Task 06 already established that the current pinned action is v7/v7.0.1 and keeps the relevant v4+ immutable artifact, missing-file, retention, and output semantics for this task.
- Win32 classic samples provide pattern guidance, not project-specific proof. Final evidence must come from this repository's `--ui-perf-test`, screenshot scripts, workflow artifacts, and real Windows review when needed.
