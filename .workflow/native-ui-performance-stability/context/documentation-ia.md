# Documentation Information Architecture

Generated: 2026-06-30T06:55:23+08:00

## Purpose

This document defines the target documentation architecture before the public docs
are rewritten. It is based on repository evidence, CI evidence, downloaded
artifact evidence, and the documentation claim audit. It is not itself a user
guide or release note.

## Source Priority

| Priority | Evidence Source | Rule |
|---:|---|---|
| 1 | Source, CMake, tests, scripts, workflows | Defines what the product can build and test now. |
| 2 | GitHub Actions artifacts and package summaries | Defines what the final downloadable executable actually contains. |
| 3 | Executable gates and screenshot artifacts | Defines what the shipped UI and function gates proved. |
| 4 | User-confirmed project requirements | Defines nonblocking versus blocking acceptance scope. |
| 5 | Existing Markdown docs | Input for rewrite only; never proof by itself. |

Primary context files:

- `.workflow/native-ui-performance-stability/context/source-inventory.md`
- `.workflow/native-ui-performance-stability/context/documentation-audit.md`
- `.workflow/native-ui-performance-stability/context/artifact-baseline.md`

## Target Documentation Set

| Target | Proposed Path | Audience | Required Evidence Links | Notes |
|---|---|---|---|---|
| Project entry | `README.md` | Users and developers | Latest final package run, package summary, UI screenshot run, testing guide | Concise landing index, current status, download/build/test entry points. |
| user-guide | `docs/user-guide.md` | Primary user: local serial/debug engineer | Final executable name, feature regression matrix, current UI screenshots | Covers using the Win32 native app, not internal architecture. |
| developer-guide | `docs/developer-guide.md` | Developer/test engineer | CMake targets, local MinGW helper, native tests, local Wine smoke paths | Covers local build, debugging, test loops, and code ownership. |
| architecture | `docs/win32-native-architecture.md` | Maintainers | Source inventory, Phase 2 UI architecture tasks, current module map | Must describe Win32 native ownership exactly and separate Qt legacy paths. |
| testing | `docs/testing-validation.md` | Developer/test engineer | CTest inventory, self-test, ui-perf, UI capture, DPI, PTY matrix | Must distinguish automated gates, smoke tests, and optional manual checks. |
| release | `docs/release-artifacts.md` | Release operator and reviewers | GitHub Actions run URL, package summary, SHA256, artifact path, package audit | Must be regenerated from final Actions artifacts, not hand-written size claims. |
| troubleshooting | `docs/troubleshooting.md` | Users and developers | Known failure signatures from gates and scripts | Covers serial access, Wine/local debug issues, screenshot/UI capture failures, package audit failures. |
| legacy | `docs/legacy-qt-notes.md` | Maintainers | Qt workflows, Qt CMake targets, Qt helper test list | Historical and parallel Qt notes only; never Win32 native release proof. |

## Existing Documentation Treatment

| Existing File | Treatment | Destination | Reason |
|---|---|---|---|
| `README.md` | Replace in place | `README.md` | Keep as concise current entry point; remove stale source-of-truth and unverified release claims. |
| `native-spec.md` | Merge then retire or convert to generated scope appendix | `README.md`, `docs/user-guide.md`, `docs/testing-validation.md` | It currently presents itself as authoritative; future scope claims must be tied to gates. |
| `docs/architecture.md` | Replace or supersede | `docs/win32-native-architecture.md` | Current file mixes correct Win32 route notes with stale Qt/core sharing claims. |
| `docs/windows-deployment.md` | Merge and supersede | `docs/release-artifacts.md` | Keep workflow/package facts, add ui-perf and artifact evidence, remove stale size snapshots. |
| `docs/windows-native-local-debug.md` | Merge and keep content lineage | `docs/developer-guide.md`, `docs/troubleshooting.md` | Local MinGW/Wine debug path remains useful but is diagnostic, not release proof. |
| `docs/windows-native-parity.md` | Move to legacy | `docs/legacy-qt-notes.md` | Parity framing is historical; final target is Win32 native artifact behavior. |
| `docs/windows-native-slimming.md` | Merge and supersede | `docs/release-artifacts.md` | Package size and forbidden-runtime checks belong to artifact evidence, not static claims. |
| `docs/windows-native-ui-validation.md` | Split and supersede | `docs/testing-validation.md`, `docs/troubleshooting.md` | Self-test, ui-perf, screenshot capture, Wine smoke, and manual checks need separate evidence levels. |
| `docs/windows-serial-validation.md` | Merge as optional/manual section | `docs/testing-validation.md`, `docs/troubleshooting.md` | Real-device validation remains a risk note but is not blocking for this workflow. |
| `docs/images/native-ui-overview.png` | Refresh or label stale | `docs/images/native-ui-overview.png` or regenerated screenshot asset | Must match final Actions UI capture or be explicitly marked historical. |

No existing public Markdown file should be deleted until Phase 5 rewrites the
docs and adds redirects or replacement links. During rewrite, old names may be
kept as compatibility stubs if external links are likely.

## Evidence Requirements By Document Type

### `README.md`

- Must state that the current release target is `svm-native-win32.exe`.
- Must link to the final `SerialValueMatcherNative-win32-native-x64.zip`
  artifact or to the release page generated from the final artifact review.
- Must link to the user-guide, developer-guide, architecture, testing, release,
  troubleshooting, and legacy documents.
- Must not claim a release is current unless Phase 6 has captured a matching
  Actions run and artifact summary.

### `docs/user-guide.md`

- Must only describe features that have passed the functional closure gates.
- Must link each major workflow to the testing matrix row that proves it:
  serial connection, logs, single send, multi/file send, Modbus scan, candidate
  analysis, report export, preferences, and UI state.
- Must use current screenshots from the final UI capture artifact or mark images
  as illustrative.

### `docs/developer-guide.md`

- Must define the supported local development paths:
  - Windows Actions MSVC package is authoritative.
  - Local MinGW/Wine is diagnostic and smoke-only.
  - Qt build/test path is parallel or legacy unless the doc is inside legacy.
- Must link to exact CMake options and scripts:
  `SVM_BUILD_WIN32_APP`, `SVM_BUILD_QT_APP`, `SVM_BUILD_QT_TESTS`,
  `scripts/build-windows-native-mingw.sh`,
  `scripts/package-windows-native-mingw.sh`, and
  `scripts/capture-windows-native-ui-wine.sh`.

### `docs/win32-native-architecture.md`

- Must describe Win32 native modules using current source ownership:
  `src/win32`, `src/core`, `src/native_storage`, `svm_win32_serial`,
  `svm_slim_core`, and `svm_native_storage`.
- Must describe planned/implemented UI architecture pieces only when they exist:
  `NativeUiState`, `NativeLayoutModel`, `NativeLayoutTransaction`,
  `NativeFrameScheduler`, `DirtyRegion`, and `PaintPolicy`.
- Must explicitly state that `src/modbus`, `src/matching`, and `src/report` are
  Qt-era paths unless future code changes rewire ownership.

### `docs/testing-validation.md`

- Must separate evidence levels:
  - CTest unit/integration tests.
  - `--self-test` executable gate.
  - `--ui-perf-test` executable gate.
  - Windows UI screenshot/capture matrix.
  - DPI smoke matrix.
  - Splitter drag frame gate.
  - Serial PTY normal/reopen/timeout/cancel matrix.
  - Optional real-device manual serial validation.
- Must state that package forbidden-runtime checks are package audit
  responsibility, not `--self-test` responsibility.

### `docs/release-artifacts.md`

- Must be regenerated after final GitHub Actions runs.
- Must include run id, run URL, commit, artifact name, zip path, SHA256, package
  summary path, extracted executable path, zip bytes, extracted bytes, file
  count, gate status, and forbidden runtime outcome.
- Must not contain hand-maintained "current size" claims without an artifact
  summary reference.

### `docs/troubleshooting.md`

- Must group failures by gate and user symptom:
  build/configure, CTest, self-test, ui-perf, UI capture, DPI/screenshot,
  splitter drag, serial PTY, package audit, local Wine/MinGW, and manual serial.
- Must include what evidence file to inspect before proposing a fix.

### `docs/legacy-qt-notes.md`

- Must include only historical or parallel Qt context.
- Must list Qt workflows and Qt helper tests as retained regression/reference
  coverage.
- Must state that Qt evidence does not prove Win32 native release UI behavior.

## Label Rules

Use these labels consistently in public docs and internal evidence tables.

| Label | Meaning | Allowed Claims |
|---|---|---|
| `current` | Proven by current source/CI/artifact evidence for the Win32 native release path. | May be used in README and user-facing docs. |
| `current-intent` | Implemented or planned in current source, but final artifact evidence is pending. | Must link to the pending gate and avoid release-ready wording. |
| `legacy` | Historical or parallel Qt-era material retained for context or regression reference. | Must not be used as Win32 native release proof. |
| `manual-optional` | Useful manual validation not blocking this workflow. | May appear in testing and troubleshooting docs only. |
| `future-plan` | Desired future work not implemented or not yet verified. | Must not appear as a shipped feature. |
| `unknown` | Claim has not been verified in the current workflow. | Must be removed from public docs or paired with an explicit verification task. |

## Rewrite Rules

1. Every user-visible "current" claim must cite source, CI, executable gate, or
   artifact evidence.
2. Static docs must not contain "latest" package sizes, screenshots, or run ids
   unless they are generated from Phase 6 evidence.
3. Qt content must be isolated under legacy unless it is explicitly about
   maintaining the parallel Qt path.
4. Screenshots must carry freshness evidence: source run id, commit, capture
   time, and whether they match the final package commit.
5. Manual real-device serial validation is valuable but nonblocking for this
   workflow because the accepted final target is the GitHub Actions executable.
6. Any future feature or unverified capability must be labeled `future-plan` or
   removed from the user-facing document.

## Phase 5 Implementation Notes

- Start by creating or replacing the target docs listed above.
- Preserve external link compatibility with short stubs if old file names are
  removed.
- Regenerate `docs/images/native-ui-overview.png` from final Actions UI capture
  or mark it as legacy/illustrative.
- Add a documentation consistency gate that fails on forbidden stale phrases,
  unsupported "latest" claims, and Qt evidence presented as Win32 native proof.

