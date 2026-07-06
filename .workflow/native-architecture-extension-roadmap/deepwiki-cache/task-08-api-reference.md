# API Reference — Task 08: Phase 1 UI Regression Closure
Generated: 2026-07-06T19:02:00+08:00

| API | Library | Source | Confidence |
|-----|---------|--------|------------|
| `path` / file path collection | GitHub Actions artifact flow / `actions/upload-artifact` | DeepWiki success; Phase 1 cache; official README | High |
| `retention-days` | GitHub Actions artifact flow / `actions/upload-artifact` | DeepWiki success; Phase 1 cache; official README | High |
| `if-no-files-found` | GitHub Actions artifact flow / `actions/upload-artifact` | DeepWiki success; Phase 1 cache; official README | High |
| immutable artifact upload, unique names, `overwrite` behavior | GitHub Actions artifact flow / `actions/upload-artifact` | DeepWiki success; official README | High for v4 architecture; Medium for project comment naming later majors |
| `artifact-id`, `artifact-url`, `artifact-digest` outputs | GitHub Actions artifact flow / `actions/upload-artifact` | DeepWiki success; official README | High |
| `ctest --test-dir ... --output-on-failure` | CMake / CTest | DeepWiki success; Phase 1 cache; CTest manual | High |
| `-C Release` for multi-config CI builds | CMake / CTest | Local workflow; CTest practice | High |
| UI evidence files and `capture-status.txt` scenarios | Project capture scripts and docs | Local scripts/docs | High |
| docs/artifact consistency gate | Project validation script | Local script | High |

## Source Notes

- DeepWiki query `actions/upload-artifact` succeeded with the required full question. It covered `path`, retention, `if-no-files-found`, immutable artifacts, outputs, compression, and UI screenshot/log artifact practices. DeepWiki noted that v5/v7 were not explicitly detailed in its provided repo context; the reliable behavior is the v4 immutable artifact model and current action inputs.
- DeepWiki query `Kitware/CMake` succeeded with the required full question. It covered `ctest --output-on-failure`, `CTEST_OUTPUT_ON_FAILURE=1`, CI usage, and CTest-produced reports such as JUnit XML. It did not find CMake-repo-specific examples for this project's UI capture scripts, so project-specific guidance below is derived from local workflow/scripts/docs.
- Phase cache used: `.workflow/native-architecture-extension-roadmap/deepwiki-cache/phase-1-research.md`.
- Local task plan used: `.workflow/native-architecture-extension-roadmap/phases/phase-1/tasks/task-08-phase-1-ui-regression-closure.md`.
- Local workflow/scripts/docs used: `.github/workflows/windows-native-ui-capture.yml`, `scripts/capture-windows-native-ui.ps1`, `scripts/capture-windows-native-ui-wine.sh`, `scripts/check-docs-artifact-consistency.py`, `docs/Windows原生UI验证.md`, `CMakeLists.txt`.
- Official references checked: `https://github.com/actions/upload-artifact/blob/main/README.md`, `https://cmake.org/cmake/help/latest/manual/ctest.1.html`.

## upload-artifact paths/retention/missing-file behavior

### `path` collection

Use explicit path entries for review-critical evidence. The current Windows UI workflow uploads:

```yaml
path: |
  artifacts/windows-native-ui/*.png
  artifacts/windows-native-ui/capture-status.txt
  artifacts/windows-native-ui/ui-perf-test.log
  artifacts/windows-native-ui/window-info.txt
```

DeepWiki and the action README agree that `path` can be a file, directory, or glob pattern. Multiple paths are collected under a common root. Hidden files are excluded by default in newer v4 behavior unless `include-hidden-files: true` is set; Task 08 UI screenshots/logs should not require hidden files.

Project implication: keep required evidence paths explicit and stable. If Task 08 adds `self-test.log` to the Windows artifact, first make the workflow or capture script write that file into `artifacts/windows-native-ui/`, then add it to both the upload path list and docs consistency expectations. Do not claim a file is in the Actions artifact if it only appears in the step log.

### `retention-days`

`retention-days` controls artifact lifetime. The current project workflow uses:

```yaml
retention-days: 14
```

DeepWiki reports the action default is repository/action retention when unset, commonly up to 90 days. Keep Task 08 evidence retention intentional. Fourteen days is enough for PR UI review; release evidence may justify longer retention in a separate release/package workflow.

### `if-no-files-found`

Valid behaviors are:

- `warn`: continue with a warning; this is the default.
- `error`: fail the action.
- `ignore`: continue with an informational message.

Task 08 evidence is release-gating evidence, so the current project setting is correct:

```yaml
if-no-files-found: error
```

Project implication: screenshots, `capture-status.txt`, `ui-perf-test.log`, and `window-info.txt` are not soft diagnostics. Missing files invalidate the UI capture and should fail CI.

### Artifact immutability and naming

DeepWiki confirms the v4 architecture is immutable: an uploaded artifact is not appended to or mutated later. Use unique, descriptive artifact names and avoid multiple uploads to the same name in one job unless using verified replacement behavior.

Current project artifact name:

```yaml
name: windows-native-ui-screenshots
```

The workflow currently uses a pinned SHA with a comment naming `actions/upload-artifact v7`. Treat the pinned SHA as the actual dependency. Do not change to a floating `@v5` or `@v7` without verifying the release/tag exists and that inputs still match the v4-compatible behavior documented here.

## CTest gate behavior

### Local gate

Task 08's local command is:

```bash
ctest --test-dir build-codex --output-on-failure
```

`--output-on-failure` keeps passing logs compact and prints failed test output for diagnosis. DeepWiki also confirms `CTEST_OUTPUT_ON_FAILURE=1` is an equivalent environment-variable route.

### Windows multi-config CI gate

The current Windows UI workflow uses a Visual Studio generator and runs:

```powershell
ctest --test-dir $env:BUILD_DIR --output-on-failure -C Release
```

Keep `-C Release` for multi-config build trees. A failing CTest process exits nonzero and must block later UI evidence steps; otherwise screenshots could be produced from a build with broken model/layout logic.

### Project test surface

`CMakeLists.txt` registers the native logic/layout tests through `add_test()`. The relevant Phase 1 closure surface includes layout metrics/model/transaction tests, frame scheduler tests, workbench tab state tests, log scroll/filter tests, native UI preference tests, serial state tests, Modbus UI/request tests, storage tests, and protocol/report tests.

Project implication: CTest proves the nonvisual model and resize/tab scheduling contracts before UI capture proves the rendered app. Do not replace CTest with screenshot evidence; they cover different failure modes.

## UI evidence checklist

A Task 08 pass should prove the same evidence chain as `docs/Windows原生UI验证.md` and the capture scripts:

- Default window: `root.png` and `PASS default-window`.
- Primary tabs: `tab-single.png`, `tab-quick.png`, `tab-file.png`, `tab-scan.png`, `tab-settings.png`, plus `PASS tab-set`.
- Compact tabs: `compact-tab-single.png`, `compact-tab-quick.png`, `compact-tab-file.png`, `compact-tab-scan.png`, `compact-tab-settings.png`, plus `PASS compact-tab-set`.
- Fast tab frames where enabled: `*-fast.png` entries are useful for repaint/switching diagnosis.
- Resize sweep: `resize-*.png` and `PASS resize-sweep`.
- DPI smoke on Windows capture: `dpi-100-window.png`, `dpi-125-window.png`, `window-info.txt`, and `PASS dpi-smoke-*`.
- Splitter drag frames: `log-splitter-before.png`, `log-splitter-frame-01.png`, `log-splitter-frame-02.png`, `log-splitter-after.png`, and `PASS splitter-drag-frames`.
- Performance evidence: `ui-perf-test.log`, generated from the current build and judged against release/artifact-derived baselines.
- Completion marker: `PASS capture-complete`.
- Self-test evidence: Windows workflow step `--self-test` must exit 0; Wine capture additionally writes `self-test.log` and `PASS self-test`.

Failure conditions that should block closure:

- `capture-status.txt` lacks `PASS tab-set`, `PASS compact-tab-set`, `PASS resize-sweep`, `PASS splitter-drag-frames`, or `PASS capture-complete`.
- Any required screenshot is missing, empty, or visually blank.
- Tab screenshots do not differ enough to prove switching.
- Resize screenshots lose toolbar/sidebar detail.
- Splitter drag frames do not differ enough to prove live movement.
- `ui-perf-test.log` is missing or not tied to the baseline-derived gate.
- `window-info.txt` is missing.
- Self-test fails.

## Project-specific script/workflow guidance

### Windows Actions ordering

The current `.github/workflows/windows-native-ui-capture.yml` order is the right closure pattern:

1. Configure native-only build.
2. Build Release.
3. Run `ctest --test-dir $env:BUILD_DIR --output-on-failure -C Release`.
4. Run `svm-native-win32.exe --self-test`.
5. Run `svm-native-win32.exe --ui-perf-test`.
6. Run `scripts/capture-windows-native-ui.ps1 -SkipUiPerfTest` so capture does not duplicate the already-blocking perf step.
7. Write `GITHUB_STEP_SUMMARY` sections for screenshot list, capture status, UI perf, baseline policy, DPI/window metrics, capture logs, and full evidence file list.
8. Run `python scripts/check-docs-artifact-consistency.py`.
9. Upload `windows-native-ui-screenshots` with `if-no-files-found: error` and `retention-days: 14`.

### Local/Wine evidence path

Use the Wine script for local smoke evidence when a Windows runner is unavailable:

```bash
SVM_WINEPREFIX=/tmp/svm-native-wine64-ui2 \
SVM_XDG_RUNTIME_DIR=/tmp/xdg-runtime-root \
SVM_WINE_UI_OUTPUT_DIR=/tmp/svm-native-wine-ui \
scripts/capture-windows-native-ui-wine.sh
```

Wine evidence includes `self-test.log`, `ui-perf-test.log`, `capture-status.txt`, `window-info.txt`, `root.png`, tab screenshots, compact screenshots, resize screenshots, and splitter drag frames. Per project docs, Wine screenshots are smoke evidence only; final visual judgment remains the GitHub Actions Windows UI artifact and real Windows screenshots.

### Docs consistency gate

Task 08 should run:

```bash
python3 scripts/check-docs-artifact-consistency.py
```

The checker enforces current artifact names, executable name, native workflow references, `--self-test`, `--ui-perf-test`, expected UI artifact files, active docs terminology, package summary terminology, and Markdown links. If workflow artifact paths change, update the docs and checker expectations together.

### Conservative closure rule

For Phase 1 closure, treat CTest, self-test, UI perf, UI capture, artifact upload, and docs consistency as one chain. A pass is only meaningful when the same build produces all evidence, required files are uploaded, and the docs describe exactly those artifacts.
