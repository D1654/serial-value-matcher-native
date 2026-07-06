# Draft Self-Review — Native Architecture Extension Roadmap

Generated: 2026-07-06T16:21:45+08:00
Scope: Phase 2 complete draft, after BS-5.

## Review Verdict

The draft is directionally sound and can proceed to Phase 3 after user approval, but it needed three precision corrections before planning:

1. Do not imply that serial PTY stress is already fully CI-blocking in GitHub Actions.
2. Make "existing validation assets" explicit so Phase 3 does not rebuild infrastructure already present.
3. Convert vague performance wording into "baseline-derived thresholds" during Phase 3 planning.

## Evidence Checked

- Existing tests: `tests/` currently contains 57 test files.
- Existing native package workflow: `.github/workflows/windows-native-package.yml`
- Existing native UI capture workflow: `.github/workflows/windows-native-ui-capture.yml`
- Existing package/docs consistency script: `scripts/check-docs-artifact-consistency.py`
- Existing serial PTY script: `scripts/run-windows-native-serial-pty-loopback.py`
- Current draft: `.workflow/native-architecture-extension-roadmap/draft-cache.md`

## Findings

### Finding 1 — Serial PTY Stress Is Not Fully CI-Blocking Yet

Severity: Medium

The native package workflow currently writes a "Native serial PTY matrix" evidence file with `Status: local-only` because the Windows runner does not expose POSIX PTYs or Wine dosdevices. The draft's production pipeline phrasing could be read as if serial simulation/stress is already a full GitHub Actions gate.

Required correction:

- Phase 3 must distinguish:
  - CI-blocking: CTest, self-test, UI perf, package audit, docs consistency.
  - Current local pre-release evidence: PTY loopback matrix until CI loopback support is implemented.
  - Future improvement: make fake transport/loopback stress CI-blocking where technically possible.

### Finding 2 — Phase 3 Should Formalize Existing Assets, Not Rebuild Them

Severity: Medium

The repo already has 57 tests, native packaging workflow, UI capture workflow, docs/package consistency checks, package audit scripts, and serial PTY loopback scripts. If Phase 3 treats these as missing foundations, the plan will waste effort and create churn.

Required correction:

- Phase 3 tasks should first inventory and harden existing tests/scripts/workflows, then add only missing gates.
- "Build new infrastructure" should be reserved for gaps that existing assets cannot cover.

### Finding 3 — UI Performance Criteria Need Baseline-Derived Thresholds

Severity: Medium

The draft uses phrases like "无明显回退". That is useful as intent but too subjective for Phase 4 execution.

Required correction:

- Phase 3 must define UI perf acceptance using current `--ui-perf-test` output as baseline.
- Thresholds should be relative to existing release/baseline artifact when possible, not invented abstract numbers.

### Finding 4 — Proposed `controllers/` Directory Must Stay Conditional

Severity: Low

Section 6 lists `src/win32/controllers/` as a target direction. This is acceptable only if controller extraction actually needs that directory. Creating a directory solely to satisfy the architecture diagram would violate the draft's own "no cosmetic moves" constraint.

Required correction:

- Phase 3 should allow controller files either adjacent in `src/win32/` or in `src/win32/controllers/`, depending on the smallest behavior-protected extraction path.

### Finding 5 — Version Metadata Drift Must Be Planned Explicitly

Severity: Medium

The draft correctly mentions version single-source governance. Existing CMake project metadata may not track the latest release tag unless made explicit in planning.

Required correction:

- Phase 3 must include a concrete version metadata task covering CMake project version, Win32 VERSIONINFO, package name, README, docs, release notes, and artifact summary.

## Final Self-Review Decision

The draft remains acceptable, but Phase 3 planning must carry these constraints as concrete tasks or acceptance criteria. The most important correction is the serial PTY gap: do not call the workflow fully closed until the Actions artifact has either a CI-capable fake/loopback stress gate or a documented local pre-release evidence requirement.
