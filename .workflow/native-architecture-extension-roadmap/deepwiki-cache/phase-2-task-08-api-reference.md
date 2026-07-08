# API Reference - Phase 2 Task 08: Backend Regression Closure

Generated: 2026-07-08T18:32:00+08:00

## Scope

Task 08 uses existing CMake/CTest and GitHub Actions artifact flow. It does not add runtime dependencies or change the native package technology stack.

## Evidence Boundary

Phase 2 closure evidence is split into CI-blocking and local release-candidate evidence:

- CI-blocking: native CTest, package audit, docs consistency, app self-test, UI performance gate, required artifact files.
- Local RC evidence: PTY normal/reopen/timeout/cancel/stress matrix, because Windows runners do not provide POSIX PTYs or Wine dosdevices.

## Project Decisions

- Preserve `ctest --output-on-failure` output as `native-ctest.log` in the package artifact.
- Add a small `phase-2-backend-regression.txt` evidence file summarizing serial queue, Modbus executor, storage recovery, self-test, UI perf, and PTY boundary evidence.
- Keep long-running stress tests opt-in through the existing `SVM_ENABLE_STRESS_TESTS` CMake gate.
