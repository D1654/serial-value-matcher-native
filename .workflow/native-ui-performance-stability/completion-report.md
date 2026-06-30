# Native UI Performance Stability Completion Report

Date: 2026-07-01T02:28:00+08:00
Project: SerialValueMatcher Native
Scope: Win32 native artifact, UI/function recovery, performance/stability gates, documentation rewrite

## Final Verdict

Engineering delivery gate: passed.

The final GitHub Actions artifact target completed package and UI capture workflows with
success. The downloaded `svm-native-win32.exe` was inspected locally, passed SHA256,
package audit, screenshot integrity review, Wine self-test, and Wine UI performance smoke.
No blocking final gate failure remained after Phase 6 review.

## Final Artifact

- Target commit: `b7a15fe7a487f22f8f30f75008d847effe40a2c3`
- Branch: `main`
- Package workflow run: `28465681222`
- UI capture workflow run: `28465775335`
- Package artifact: `SerialValueMatcherNative-win32-native-x64`
- UI artifact: `windows-native-ui-screenshots`
- Local package path: `artifacts/github-actions/final-package-28465681222/`
- Local UI path: `artifacts/github-actions/final-ui-28465775335/`
- Zip: `artifacts/github-actions/final-package-28465681222/SerialValueMatcherNative-win32-native-x64.zip`
- Zip SHA256: `A7DDF98FFFAA4E5207C3D68215A6FACB1F006DF3B89FFC1F1309376F732EC336`
- Executable: `artifacts/github-actions/final-package-28465681222/extracted/svm-native-win32.exe`
- Executable SHA256: `9aa86fc4ce0b431744ca7136bcb44ccfe660133cccb72a04dbe810933f236282`

## Gate Evidence

Package run `28465681222`:

- Build Release: success
- Native tests: success
- Native serial PTY matrix report: success, with Windows-runner `local-only` limitation recorded
- Native app self-test: success
- Native UI performance gate: success
- Package native zip: success
- Package audit: success
- Docs artifact consistency: success
- Upload artifact: success

UI capture run `28465775335`:

- Build Release: success
- Native tests: success
- Native app self-test: success
- Native UI performance gate: success
- UI screenshot capture: success
- Screenshot summary: success
- Docs artifact consistency: success
- Upload UI screenshots: success

Downloaded artifact review:

- `svm-native-win32.exe` present in zip.
- ZIP SHA256 matched the sidecar file.
- Package summary reports `Gate status: passed`.
- Forbidden Qt/SQLite/.NET runtime files: none.
- Unicode text probe: passed.
- `native-self-test.log`: `ok`.
- `native-ui-perf-test.log`: `ui-perf ok tabs=300 tab-ms=1110 layout-pass=12 apply=324 revision=12 log-lines=1200 log-ms=28 log-flush=2 log-rebuild=1`.
- UI capture artifact contains 22 original screenshots covering default, DPI smoke, standard tabs, compact tabs, resize sweep, and splitter drag frames.
- Screenshot pixel checks and visual review found populated tabs, no blank tab pages, no broken resize blocks, and live splitter height changes.

Local verification:

- `ctest --test-dir build-codex --output-on-failure`: 49/49 passed.
- Downloaded exe Wine self-test: exit code `0`, log `ok`.
- Downloaded exe Wine UI perf: exit code `0`, log `ui-perf ok tabs=300 tab-ms=2721 layout-pass=12 apply=324 revision=12 log-lines=1200 log-ms=314 log-flush=2 log-rebuild=1`.

## Completed Engineering Work

UI architecture and performance:

- Introduced native layout ownership boundaries around layout model, layout transaction, frame scheduling, and paint policy.
- Stabilized tab content visibility after tab switching and first open.
- Stabilized resize and splitter drag behavior with reduced redraw churn.
- Added splitter drag frame evidence and resize/DPI screenshot coverage.

Functional closure:

- Expanded native self-test coverage for serial state, log state, send workflows, file send, Modbus scan, analysis, rule/report flow, storage, and UI state.
- Added local serial PTY loopback scenarios for normal, reopen, timeout, cancel, and stress coverage.
- Preserved current Win32 native feature scope without adding product features or changing the UI style.

CI and artifact hardening:

- Integrated Windows native package artifact evidence.
- Added package summary, SHA256 sidecar, forbidden runtime checks, Unicode probe, native self-test log, UI perf log, and serial PTY matrix file.
- Integrated Windows native UI capture artifact with screenshot matrix, capture status, UI perf log, and window info.
- Added docs artifact consistency gate to package and UI capture workflows.

Documentation:

- Rebuilt user, developer, architecture, testing, release artifact, troubleshooting, and legacy Qt documentation around the current Win32 native reality.
- Clarified that the current user-facing artifact is `SerialValueMatcherNative-win32-native-x64.zip` with `svm-native-win32.exe`.
- Kept Qt documentation as legacy context, not the current release route.
- Added documentation consistency checks to prevent future drift back to stale Qt/package naming.

## Residual Risks

- Real hardware serial behavior is not fully covered by GitHub Actions. USB serial chip behavior, DTR/RTS, hardware flow control, hot plug, and long device sessions still require physical Windows validation.
- Windows Actions runner cannot expose POSIX PTYs or Wine dosdevices, so the package artifact records PTY matrix as `local-only`. Local PTY stress exists, but it is not a Windows-runner hardware substitute.
- DPI evidence is limited by runner behavior: captured runner DPI was 96 and direct DPI switching was unavailable, so 125% evidence is a scaled-window smoke path rather than a full native high-DPI desktop session.
- UI performance gates are application-level regression gates, not ETW/WPR/WPA frame-time traces.
- No code signing, notarized provenance, release attestation, SBOM publication, or SmartScreen reputation workflow is implemented.
- No 8-hour or 24-hour endurance run has been added.

## Future Hardening

- Add code signing and release provenance attestation for published zip files.
- Add SBOM and dependency provenance checks to the release artifact.
- Add WPR/WPA or ETW-based UI profiling for frame pacing, paint cost, and message-loop stalls.
- Add a physical Windows serial rig for USB serial, DTR/RTS, flow control, hot plug, and long-running traffic.
- Expand the Windows matrix across Windows 10/11, 100%/125%/150% DPI, and more display sizes.
- Add long-run stress for high-volume logs, file send, Modbus scan cancellation, and reconnect loops.
- Add a release-promotion workflow that copies the exact passing Actions artifact into a GitHub Release and records run id, commit, SHA256, and package summary.

## References

- Final Actions review: `.workflow/native-ui-performance-stability/context/final-actions-review.md`
- Final artifact review: `.workflow/native-ui-performance-stability/context/final-artifact-review.md`
- Release artifact documentation: `docs/release-artifacts.md`
- Testing documentation: `docs/testing-validation.md`
