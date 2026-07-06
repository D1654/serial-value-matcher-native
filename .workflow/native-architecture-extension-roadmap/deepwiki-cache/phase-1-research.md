# DeepWiki Research Cache - Phase 1: UI / Architecture Foundation

Generated: 2026-07-06T16:59:44+08:00

## Repo Map

- Win32 API / Windows samples -> `microsoft/Windows-classic-samples`
- CMake / CTest -> `Kitware/CMake`
- GitHub Actions artifact flow -> `actions/upload-artifact`

## microsoft/Windows-classic-samples

### Structure

Relevant pages:
- Windows Classic Samples Overview
- UI and Shell Integration
- DPI Awareness
- Win32 Fundamentals

### Research

**Q:** Core Win32 UI message, DPI/layout, repaint, and resize best practices relevant to stabilizing a lightweight native desktop application's tabbed UI and avoiding resize flicker.

**A summary:**
- Use tab selection notifications to hide the previous child, show the selected child, and then resize the active content area.
- Keep resize behavior centered on `WM_SIZE` and compute child bounds once per resize.
- Use batched child-window movement, especially `BeginDeferWindowPos`, `DeferWindowPos`, and `EndDeferWindowPos`, to reduce repeated redraw and flicker.
- Handle background erase carefully; unnecessary erase before repaint is a common flicker source.
- Keep drawing inside `BeginPaint` / `EndPaint`, invalidate only the needed regions, and handle DPI changes explicitly when the app supports DPI scaling.

**Phase 1 implication:** UI/layout tasks should treat resize, tab visibility, prompt visibility, and repaint policy as one evidence chain, not isolated visual fixes.

## Kitware/CMake

### Structure

Relevant pages:
- Overview
- Testing Framework
- CTest Architecture
- Test Infrastructure
- CI/CD Pipeline

### Research

**Q:** Core CMake and CTest practices relevant to a C++20 Win32 native project that validates UI/performance scripts and keeps test registration reliable.

**A summary:**
- Register tests with `add_test()` and use `set_tests_properties()` for labels, dependencies, fixtures, and pass/fail output matching when useful.
- Use `ctest --output-on-failure` in CI and local verification so passing logs stay small while failures remain diagnosable.
- Keep CI test environments isolated and avoid user-wide state leaking into test behavior.
- Be explicit about platform-specific exclusions or opt-in long-running tests; unstable or environment-dependent tests should not masquerade as always-blocking gates.
- On Windows CI, avoid workflows that can hang on interactive error dialogs.

**Phase 1 implication:** UI perf/capture evidence should be documented as baseline-derived, reproducible commands and artifacts, with environment-dependent checks separated from blocking gates.

## actions/upload-artifact

### Structure

Relevant pages:
- Upload Artifact Action
- Usage and Configuration
- File Selection and Processing
- Migration from v3 to v4

### Research

**Q:** Core `upload-artifact@v4` inputs and best practices for preserving Windows UI screenshots, logs, performance outputs, and failing when expected evidence files are missing.

**A summary:**
- Use descriptive artifact names and explicit `path` entries for screenshots, logs, performance output, summaries, and validation reports.
- Set `if-no-files-found: error` for evidence that must exist for a valid review.
- Use `retention-days` intentionally rather than relying on unclear defaults.
- Use unique artifact names in v4; artifacts are immutable and multiple uploads to the same name can fail unless replacement is intended.
- Adjust compression when large or already-compressed evidence would slow uploads.

**Phase 1 implication:** UI evidence artifacts should fail loudly when required screenshots/logs are missing, and artifact names should stay stable enough for README/docs/runbook references.

## Cross-Repo Integration

**Q:** Integration patterns and common pitfalls for a Win32 native UI project that uses CTest and GitHub Actions upload-artifact to preserve UI screenshots, resize/performance logs, and validation evidence.

**A summary:**
- Let the test/capture harness produce structured evidence files first, then upload those files with explicit artifact paths.
- Give artifacts unique and descriptive names, especially when matrix jobs or multiple capture modes exist.
- Do not rely on v3-style mutable artifact merging behavior; v4 artifact uploads need unique names or an explicit merge flow.
- Treat missing critical evidence as a workflow failure.
- Keep screenshots, performance logs, and validation summaries grouped so reviewers can inspect the exact UI artifact associated with a build.

## Execution Notes

- Phase 1 may use this cache for task-level research.
- Task-specific DeepWiki API references remain mandatory for each task where the Dependencies table lists external APIs or framework behavior.
