# Documentation Claim Audit

Generated: 2026-06-30T06:45:03+08:00

Status: superseded for current release evidence by the final v1.0.1 artifact review and release verification. Historical `unknown` entries below describe the evidence state at generation time, before the GitHub Actions package, UI capture, and downloaded Release assets were inspected.

## Basis

This audit checks existing documentation against current repository evidence. It does not treat documentation as acceptance criteria.

- Source inventory: `.workflow/native-ui-performance-stability/context/source-inventory.md`
- Build evidence: `CMakeLists.txt`
- CI evidence: `.github/workflows/*.yml`
- Script evidence: `scripts/*`
- Executable flags: `src/win32/main.cpp`, `src/win32/main_window_self_test.cpp`
- Current docs inspected:
  - `README.md`
  - `native-spec.md`
  - `docs/architecture.md`
  - `docs/windows-deployment.md`
  - `docs/windows-native-local-debug.md`
  - `docs/windows-native-parity.md`
  - `docs/windows-native-slimming.md`
  - `docs/windows-native-ui-validation.md`
  - `docs/windows-serial-validation.md`

## Classification

| Status | Meaning |
|---|---|
| `verified` | Current source, CMake, workflow, script, or executable flag supports the claim. |
| `stale` | Current evidence contradicts the claim or the claim is materially incomplete. |
| `historical` | Claim describes Qt-era/manual/reference material that may be useful but is not current release proof. |
| `unknown` | Claim may be true, but this task has not proven it with current executable/artifact/device evidence. |

## Cross-Document Findings

| Claim Area | Classification | Evidence | Required Treatment |
|---|---|---|---|
| Win32 native is the current final release route for this stabilization workflow. | `verified` | `windows-native-package.yml`, `windows-native-ui-capture.yml`, `svm-native-win32` target. | Keep and make it more explicit in rewritten docs. |
| Qt source and CI paths remain present. | `verified` | `SVM_BUILD_QT_APP`, `SVM_BUILD_QT_TESTS`, Qt workflows, `src/app`, `src/storage`, Qt helper tests. | Move to legacy/parallel section. Do not use as Win32 native UI proof. |
| Release package name is `SerialValueMatcherNative-win32-native-x64.zip`. | `verified` | Native workflow env `PACKAGE_NAME`; native package script package name. | Keep. |
| Final executable is `svm-native-win32.exe`. | `verified` | CMake target, native package script, workflows. | Keep. |
| No Qt/SQLite/.NET runtime in native package. | `verified` for package audit; `unknown` for latest artifact until Task 03 | `inspect-windows-package.ps1`, `inspect-windows-package.py`; latest artifact not captured yet. | Keep as a gate, but tie pass/fail to package summary. |
| `--self-test` verifies no Qt or .NET runtime is loaded. | `stale` | `runSelfTest()` checks Unicode, layout probes, serial option helpers, native storage. Forbidden runtime is package audit responsibility. | Rewrite UI validation docs to separate self-test from package audit. |
| `--ui-perf-test` exists and checks tab/control visibility, layout stability, splitter hit target, tab switching, log flush/trim behavior. | `verified` | `main.cpp`, `main_window_self_test.cpp`, native workflows. | Keep; expand only after Phase 4 matrix changes. |
| Previous Release URL and latest Release package status. | `unknown` | Not verified in this task; Task 03 will inspect Actions/release artifacts. | Superseded by final v1.0.1 artifact review and release verification. |
| Manual real-device serial validation is required before every formal release. | `historical` for current workflow | User requirement says final test target is GitHub Actions executable and manual testing is not blocking in this pass. | Keep as optional/future manual checklist, not a blocking acceptance criterion for this project. |
| Current docs are authoritative source of truth. | `stale` | Workflow source-of-truth rule requires source/CI/artifact/user feedback over docs. | Remove from rewritten docs. |

## Per-File Audit

### `README.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Product is a Chinese Windows native serial debugging and value matching tool. | `verified` | Product strings in `CMakeLists.txt`; Win32 native source and workflows. | Keep. |
| Current main package is Win32 native, no Qt runtime, SQLite plugin, or .NET/C# runtime. | `verified` as intended package gate; latest artifact `unknown` | Native workflow/package scripts and forbidden runtime inspectors. | Keep but attach to package summary evidence. |
| Previous Release page is the current download source. | `unknown` | Not inspected in this task. | Superseded by current README and v1.0.1 release guide updates. |
| UI preview image represents current UI. | `unknown` | `docs/images/native-ui-overview.png` exists, but screenshot freshness is not proven. | Replace or revalidate after UI capture phase. |
| Current capabilities list: serial, logs, send, Modbus, analysis, reports. | `unknown` for functional completeness; source-present | Source modules/tests exist, but Phase 3 functional closure will verify behavior. | Keep as intended feature list only after regression gates pass. |
| Qt source remains for history and partial automated tests. | `verified` | Qt CMake options, Qt workflows, Qt helper tests. | Keep in legacy/parallel section. |
| Local MinGW package defaults to Wine self-test and UI perf gate. | `verified` | `scripts/package-windows-native-mingw.sh`. | Keep in developer/testing docs. |
| `native-spec.md` is current product spec and boundary. | `stale` | User and workflow source-of-truth rule say docs are stale until audited. | Replace with new documentation IA after Phase 5. |

### `native-spec.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Actual project state is based on source, Release, and this document. | `stale` | Source/CI/artifact are authoritative; this doc is under audit. | Remove "this document" as source of truth. |
| Current release shape is Win32 native x64 portable package with no Qt/SQLite/.NET runtime. | `verified` as build/package intent; latest artifact `unknown` | Native target/workflows/package audit scripts. | Keep after tying to package summary. |
| Current feature scope covers serial terminal, logs, send modes, Modbus FC03/FC04, candidate analysis, rule validation, Markdown report. | `unknown` for actual closure | Source modules exist; final behavior must be proven by Phase 3/4 gates. | Rewrite as "supported after gates pass" with test references. |
| Native file storage saves raw I/O, serial config, history, scan, candidate, and validation results. | `unknown` / partly `verified` | Self-test covers raw event, serial profile, send history, UI preferences. It does not prove every listed domain object. | Split into verified storage objects and unverified claims. |
| Visible log uses bounded cache to avoid unbounded UI growth. | `verified` | `--ui-perf-test` checks log entry limit and rebuild count. | Keep with executable flag evidence. |
| Modbus scanning only uses FC03/FC04 read functions. | `unknown` in this task | Planned for functional closure; source likely supports this, but not audited here. | Verify in Phase 3 before keeping as hard claim. |

### `docs/architecture.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Repository keeps Win32 native release route and Qt historical baseline route. | `verified` | CMake targets and workflows. | Keep. |
| Previous user-facing release is Win32 native. | `unknown` for latest Release artifact; `verified` for current build path | Workflows and package scripts support Win32 native; release artifact not captured here. | Superseded by final v1.0.1 artifact review and release verification. |
| Win32 native architecture uses `NativeMainWindow`, Win32 serial, native storage, slim core, UTF conversion. | `verified` | `src/win32`, `svm_win32_serial`, `svm_native_storage`, `svm_slim_core`. | Keep and expand around new UI architecture after Phase 2. |
| Qt code is retained for history and some automated tests. | `verified` | Qt options/workflows/helper tests. | Move to legacy architecture notes. |
| Analysis logic is in core modules reusable by Qt and Win32. | `stale` / overgeneralized | Win32 native links `src/core/analysis_core.*`; Qt path currently links `src/matching/*` and related modules. | Rewrite exact ownership; avoid implying identical shared core. |
| Directory table marks `src/modbus`, `src/matching`, `src/report` as Qt route. | `verified` for current CMake linkage | These directories are in `svm_native_core`, not `svm-native-win32`. | Keep but clarify relationship to Win32 core counterparts. |

### `docs/windows-deployment.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Recommended release artifact is GitHub Actions Win32 native x64 portable package. | `verified` | Native package workflow. | Keep. |
| Workflow triggers on push to `main`, pull request, and manual dispatch. | `verified` | `windows-native-package.yml`. | Keep. |
| Workflow configures Visual Studio 2022 x64 with Qt off and Win32 native on. | `verified` | `windows-native-package.yml`. | Keep. |
| Main workflow steps list omits `--ui-perf-test`. | `stale` / incomplete | Workflow has a separate `Run native UI performance gate` step. | Rewrite step list. |
| Package gate checks executable, forbidden Qt/SQLite/.NET runtime, Unicode text, and size. | `verified` | `inspect-windows-package.ps1`, Python inspector. | Keep. |
| Recent local MinGW package size is about 673 KiB/1.70 MiB and passed. | `unknown` | Not tied to current artifact in this task. | Move to artifact baseline or remove from stable docs. |
| Release text should avoid literal `\n`. | `historical` | Maintenance note, not current build behavior. | Move to release checklist/troubleshooting. |

### `docs/windows-native-local-debug.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Official package is GitHub Actions Windows MSVC, local MinGW is diagnostic only. | `verified` | Native workflow and local scripts. | Keep. |
| MinGW build output is `build-windows-native-mingw/svm-native-win32.exe`. | `verified` | `scripts/build-windows-native-mingw.sh`. | Keep. |
| MinGW package script defaults to Wine self-test and UI perf gate and writes `Wine gate status`. | `verified` | `scripts/package-windows-native-mingw.sh`. | Keep. |
| Wine UI capture switches tabs and checks screenshots non-empty/non-white. | `verified` as script intent | `scripts/capture-windows-native-ui-wine.sh`. | Keep as smoke path; do not make it final visual proof. |
| Dependency installation command is sufficient on every Debian 12 environment. | `unknown` | Environment-specific; not validated here. | Keep as "known setup" with troubleshooting caveat. |

### `docs/windows-native-parity.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| User release package is Win32 native and Qt remains historical/reference. | `verified` as current route | CMake/workflows/source inventory. | Keep. |
| Native covered features include serial, send, logs, Modbus, candidate analysis, reports, CI package gate. | `unknown` for actual behavior closure | Source/tests exist; Phase 3/4 must prove end-to-end function and UI. | Keep only after regression gates, or mark as intended coverage. |
| True device validation remains needed for high-speed serial, DTR/RTS, large scans, candidate accuracy. | `historical` / risk note | User requirement does not make manual validation blocking for this project. | Move to future/manual validation appendix. |

### `docs/windows-native-slimming.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Lightweight goal is Win32 native without bundling Qt/.NET runtime. | `verified` | Native CMake/workflow/package scripts. | Keep. |
| Package name and executable name. | `verified` | Native package script/workflow. | Keep. |
| Current local MinGW size around 673 KiB zip and 1.70 MiB extracted. | `unknown` | Not captured in this task. | Move to artifact baseline or replace with generated package summary. |
| Gate status passed. | `unknown` for current artifact | Latest package summary not captured in this task. | Replace with final artifact evidence. |
| Gate thresholds zip <= 5 MB, extracted <= 8 MB, no Qt/SQLite/.NET runtime. | `verified` | Inspect scripts default thresholds and forbidden checks. | Keep. |

### `docs/windows-native-ui-validation.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| `svm-native-win32.exe --self-test` exists and exits 0 on pass. | `verified` | `main.cpp`, `runSelfTest()`. | Keep. |
| Self-test does not pop a user window. | `verified` by code path | `main.cpp` returns from self-test before normal window loop. | Keep. |
| Self-test checks no Qt or .NET runtime. | `stale` | Runtime/package checks live in package inspectors, not `runSelfTest()`. | Rewrite. |
| Wine capture output names and nonblank screenshot expectations. | `verified` as smoke script intent | `capture-windows-native-ui-wine.sh`. | Keep as Wine smoke, not final proof. |
| Manual UI requirements for tabs, log behavior, scan, small window, typography, layout. | `unknown` | Must be proven by Windows UI capture matrix and executable UI perf gates. | Use as acceptance checklist only after automation refresh. |
| Failure cases block release:乱码, blank tabs, clipped controls, missing runtime, self-test failure. | `verified` as policy intent; runtime/self-test split needed | Package audit/UI capture/self-test support parts of this. | Rewrite into explicit automated gates. |

### `docs/windows-serial-validation.md`

| Claim | Classification | Evidence | Rewrite Action |
|---|---|---|---|
| Wine and CI cannot replace real hardware serial validation. | `verified` as risk statement | Source inventory shows PTY/Wine are synthetic; real device behavior is outside CI. | Keep as manual validation caveat. |
| Windows 10/11 x64 plus USB serial device checklist. | `historical` / manual checklist | Not executed in this workflow. | Move to optional/manual validation guide. |
| Basic serial, logs, Modbus, long-run validation should be recorded before every formal release. | `historical` for this project pass | User selected GitHub Actions executable as final test target and has no manual test time. | Keep as future release hardening, not current blocking criterion. |

## Sections To Rewrite, Move, Or Delete

| File | Action |
|---|---|
| `README.md` | Rewrite around current Actions artifact, concise feature list, current docs IA, and explicit "Qt legacy/parallel" note. Remove stale source-of-truth language. |
| `native-spec.md` | Replace with either a current product scope document tied to tests, or move content into user/developer guides. Do not call it authoritative until regenerated. |
| `docs/architecture.md` | Rewrite after Phase 2 to include `NativeUiState`, `NativeLayoutModel`, `NativeLayoutTransaction`, `NativeFrameScheduler`, `DirtyRegion/PaintPolicy`, and corrected Qt/core ownership. |
| `docs/windows-deployment.md` | Rewrite workflow steps to include UI perf, artifact evidence, package summary, and final artifact download process. |
| `docs/windows-native-local-debug.md` | Keep mostly, but clarify Wine is smoke only and update commands after Phase 4 gates. |
| `docs/windows-native-parity.md` | Move to `legacy-qt-notes` or appendix. Do not present parity claims as current release proof. |
| `docs/windows-native-slimming.md` | Replace hand-written size numbers with package summary/artifact evidence. |
| `docs/windows-native-ui-validation.md` | Split self-test, UI perf, Windows screenshot matrix, Wine smoke, and manual checks into separate evidence levels. |
| `docs/windows-serial-validation.md` | Move to manual validation/troubleshooting; keep nonblocking for this project pass unless future release policy changes. |

## Audit Conclusion

The documentation mostly points in the right direction, but it mixes verified source/CI facts with unverified artifact claims, manual validation expectations, and Qt-era context. The most important stale claims are:

- `native-spec.md` treating itself as a current source of truth.
- UI validation implying `--self-test` checks forbidden runtimes.
- Deployment and size docs quoting current release/package results without a captured current artifact baseline.
- Feature coverage documents presenting behavior as complete before Phase 3/4 regression gates prove it.

Until Phase 5 rewrite is complete, no stale or unknown document claim should be used as an acceptance criterion.
