# API Reference - Phase 3 Task 08: Release Runbooks and Final Documentation
Generated: 2026-07-10T11:07:08+08:00

## Summary

Task 08 is a documentation-only release runbook task. I reviewed the required Context Bus files, Phase 3 research cache, the Task 08 plan, `README.md`, the Task 08 target docs, and related auxiliary docs for UI, local debugging, package size, and real serial-device acceptance.

No DeepWiki `ask` was executed. The Task 08 dependency table explicitly declares:

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Documentation-only task. |

Because the dependencies are `None / None / None` and the task is documentation-only, adding a DeepWiki query would introduce an external research dependency that the task table does not require. Phase-level research already covers the stable CMake, upload-artifact, and Win32 evidence concepts needed to interpret the current docs.

Current read-only docs consistency status:

```text
docs consistency ok
```

The current documentation already has strong coverage for build, package evidence, UI capture, PTY serial evidence boundaries, artifact verification, and diagnostics/redaction basics. The main Task 08 gaps are release operator procedure, rollback/re-release procedure, and a more explicit diagnostics/runbook bridge from user-facing evidence bundles to troubleshooting and release triage.

## Dependencies table interpretation

- This task should not add code, workflow, script, or external library assumptions.
- Documentation updates should be based on repository-local facts: current artifact names, workflow names, package summary fields, evidence files, diagnostic bundle behavior, and docs consistency gate output.
- The stable source of release identity remains `cmake/svm_version.cmake`, but Task 08 should only document that behavior; it should not change version metadata.
- The stable current package target is the GitHub Actions Windows native package artifact, not local MinGW output and not the legacy Qt package.
- `actions/upload-artifact` semantics are already covered by Phase 3 / Task 06 / Task 07 research. Task 08 should preserve the documented distinction between Actions `artifact-digest` and the package zip SHA256 sidecar.
- PTY loopback remains local-only release-candidate evidence for normal/reopen/timeout/cancel/stress, while the Windows package workflow uploads a documented evidence file and summary for that boundary.

## Documentation coverage checklist

| Required stable item | Current coverage | Task 08 must preserve | Gap / recommended documentation action |
|----------------------|------------------|-----------------------|----------------------------------------|
| Build | `README.md`, `docs/开发者指南.md`, `docs/测试与验证.md`, `docs/Windows发布说明.md`, `docs/Windows原生本地调试.md` cover Windows MSVC native-only build, Linux/MinGW local build, CTest, self-test, UI perf, and package commands. | Current target is `svm-native-win32.exe`; CMake flags are `SVM_BUILD_QT_APP=OFF`, `SVM_BUILD_QT_TESTS=OFF`, `SVM_BUILD_WIN32_APP=ON`; official package is GitHub Actions MSVC output. | Consolidate the release build runbook so an operator can follow: trigger/run workflow, inspect workflow result, download artifact, verify package, then decide release candidate status. Keep README concise and link to detailed docs. |
| Artifact verification | `docs/发布产物.md` is comprehensive: artifact names, required files, zip SHA256, package summary fields, required docs file set, `artifact-id`, `artifact-url`, `artifact-digest`, and release-candidate disqualifiers. `docs/测试与验证.md` explains upload evidence limits. | Artifact name `SerialValueMatcherNative-win32-native-x64`; zip `SerialValueMatcherNative-win32-native-x64.zip`; sidecar `.zip.sha256.txt`; package summary `.package-summary.txt`; `Gate status: passed`; `Unexpected DLL files: none`; `Required package files: passed`; `Package documentation file set: passed`. | Add an operator checklist that ties package artifact, UI artifact, summary fields, and release notes together before release creation/update. Keep `artifact-digest` described as CI upload-object evidence, not user-facing zip integrity. |
| UI review | `docs/测试与验证.md` and `docs/Windows原生UI验证.md` cover Windows UI capture workflow, `windows-native-ui-screenshots`, required screenshots, `capture-status.txt`, `window-info.txt`, `self-test.log`, `ui-perf-test.log`, and `ui-evidence-summary.txt`. | UI capture must include default window, tab set, compact tab set, resize sweep, DPI smoke, splitter drag frames, `BaselinePolicy=release-artifact-derived`, and `GateStatus=passed`. Wine screenshots are diagnostic smoke evidence, not final visual proof. | Release runbook should say exactly which UI files/tokens block release and which screenshots need human review for Chinese text, clipping, blank/white frames, tab mismatch, resize, DPI, and splitter behavior. |
| Serial stress | `docs/测试与验证.md`, `docs/故障排查.md`, and `docs/Windows串口真机验收.md` cover native serial tests, PTY matrix command, local-only classification, summary file, and real Windows serial-device acceptance. | CI does not execute full POSIX PTY/Wine normal/reopen/timeout/cancel/stress matrix; package artifact documents this boundary. Local PTY success should include `GateStatus=passed` and `Classification=local-only-release-candidate-evidence`. Real hardware remains required for USB serial behavior confidence. | Add a release checklist decision: serial-affecting changes require local PTY matrix and, where feasible, real Windows hardware notes before release. Do not describe PTY as CI-blocking execution until workflow reality changes. |
| Release | `docs/Windows发布说明.md` has basic release prerequisites; `docs/发布产物.md` has release-candidate criteria. README links current v1.0.4 release. | Release package must be generated by `windows-native-package.yml`; release docs must match version, tag, package summary, artifact names, and SHA256. Release body must use real Markdown newlines. | Main gap. Add a clear release runbook: verify `main`, run IDs, package artifact, UI artifact, docs consistency, package summary, SHA256, release notes, attachment list, and final smoke evidence. Include create/update guidance using `gh release view/list/create/edit/upload` only as documentation, not as a script change. |
| Rollback / re-release | Current docs do not provide a concrete rollback or re-release procedure. `docs/发布产物.md` lists disqualifiers, and `docs/故障排查.md` covers artifact issues, but there is no operator sequence for bad release response. | Rollback should be documentation/policy only unless existing release assets are intentionally changed by the operator. Stable safe actions: mark release as withdrawn/pre-release or update release notes, restore previous known-good release pointer in docs if needed, keep bad artifact evidence for triage, and never silently replace a zip without a new artifact/run/hash trail. | Main gap. Add rollback section to release docs: detect issue, freeze downloads or clearly mark release, identify last known-good tag/artifact, preserve failing run IDs/SHA256/logs, decide re-release with new tag or corrected release note, rerun full evidence gates, and document what changed. |
| Diagnostics / redaction | `docs/用户指南.md` and `docs/故障排查.md` cover local diagnostic bundle export, complete vs redacted bundle, no upload/no telemetry, raw events/path/device redaction, and manual review responsibility. Troubleshooting also lists evidence needed for startup, serial, UI, Modbus, package, PTY, artifact, and docs conflicts. | Diagnostic bundle export is local-only. Redacted bundle may remove absolute paths, device identifiers, and raw business payloads; choosing payload redaction causes report body replacement with a redaction note. Human review is still required before sharing. | Strengthen runbook wording around startup/crash/session recovery evidence: zip SHA256, package summary, self-test, UI perf, screenshots, raw events, report metadata, session/config files, and what to redact before external sharing. Keep privacy boundaries explicit. |
| Docs consistency | `docs/测试与验证.md` and `docs/故障排查.md` document `python3 scripts/check-docs-artifact-consistency.py`; current read-only run returned `docs consistency ok`. | Expected Task 08 verification output in the task file is `No docs/artifact consistency failures.`, while the current script prints `docs consistency ok`. | Implementation should either preserve the actual current command output in docs or align wording carefully. Do not document an expected string that differs from the script unless the script is intentionally changed by a separate task. |

## Implementation recommendations

1. Keep docs Chinese-first and operator-oriented.
   - README should remain concise: current package, version, executable, quick download/run path, and links.
   - Detailed procedures belong in `docs/开发者指南.md`, `docs/测试与验证.md`, `docs/发布产物.md`, `docs/Windows发布说明.md`, and `docs/故障排查.md`.

2. Preserve current stable release facts.
   - Package artifact: `SerialValueMatcherNative-win32-native-x64`.
   - Zip: `SerialValueMatcherNative-win32-native-x64.zip`.
   - Executable: `svm-native-win32.exe`.
   - UI artifact: `windows-native-ui-screenshots`.
   - Workflow names: `windows-native-package.yml` and `windows-native-ui-capture.yml`.
   - Retention for Actions artifacts: 14 days.
   - Legacy Qt route is reference-only and must not be described as the current release route.

3. Add a release operator checklist.
   - Confirm `main` and intended tag/version.
   - Confirm package workflow success.
   - Download package artifact.
   - Verify zip SHA256 sidecar.
   - Read package summary and require `Gate status: passed`.
   - Confirm required evidence files exist and are non-empty.
   - Confirm UI capture workflow success and `ui-evidence-summary.txt` `GateStatus=passed`.
   - Review key screenshots manually.
   - If serial behavior changed, run local PTY matrix and preserve summary.
   - Create/update release with the zip, SHA256 sidecar, package summary, and concise release notes.

4. Add rollback and re-release guidance.
   - Do not silently overwrite a public zip with a different binary/hash.
   - Preserve failing run ID, artifact name, zip SHA256, package summary, UI evidence, and diagnostics.
   - Mark the bad release clearly or remove it from recommended status according to project policy.
   - Point users to the last known-good release while triage is active.
   - Re-release only after rerunning package, UI, docs consistency, and any serial-specific local evidence gates.
   - Prefer a new tag for changed binaries; if only release text changes, document that no binary changed.

5. Make diagnostics and redaction practical.
   - Document when to export a full bundle versus a redacted bundle.
   - List sensitive fields to review: absolute paths, device identifiers, raw TX/RX, report metadata, customer names, screenshots, and business payloads.
   - State that redaction is local and best-effort; human review is still required before sharing.
   - Tie common failures to evidence: startup uses zip/SHA/package summary/self-test; UI uses screenshots/status/window info/UI perf; serial uses parameters/device/PTY summary; Modbus uses slave/function/address range/timeouts.

6. Do not overstate unimplemented hardening.
   - Keep code signing, supply-chain attestation, WPR/WPA traces, 8h/24h stress, and multi-device hardware matrix in "not currently implemented" sections unless a separate task implements them.
   - Do not describe PTY normal/reopen/timeout/cancel/stress as GitHub Actions-executed.
   - Do not treat `artifact-digest` as replacement for package zip SHA256.

7. Verification for the implementation agent.
   - After editing docs, run:

```bash
python3 scripts/check-docs-artifact-consistency.py
```

   - Current repository output is:

```text
docs consistency ok
```

   - If the task-level expected output remains `No docs/artifact consistency failures.`, verify whether that expectation is stale before changing docs around it.
