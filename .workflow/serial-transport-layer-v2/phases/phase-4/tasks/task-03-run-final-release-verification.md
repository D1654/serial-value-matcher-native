# Task 03: Run Final Release Verification

> Phase: 4 — Boundary and Release Closure
> Status: Completed

---

## Objective

Run the complete host, MinGW/Wine, PTY, package, documentation, and CI evidence matrix and record an honest completion report for serial transport v2.

## Files

**Create:**

- `.workflow/serial-transport-layer-v2/completion-report.md`

**Modify:**

- `.workflow/serial-transport-layer-v2/state.json`

**Test:**

- Full repository verification matrix described below.

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| CMake/CTest | `Kitware/CMake` | `cmake -S/-B`, `cmake --build`, `ctest --test-dir` | Fresh host and MinGW configuration, build, and test execution. |
| Wine | `wine-mirror/wine` | `wine`, Wine COM mapping, executable exit codes | Run Windows-native serial, self-test, UI-performance, and package evidence on Linux. |
| GitHub Actions | `actions/runner` | workflow dispatch, run logs, artifacts | Confirm Windows package and UI-capture workflows pass on the final tree. |

## Steps

### Step 1: Prepare an Evidence Inventory

Create the completion report skeleton with commit/diff identity, environment versions, required commands, expected artifacts, and explicit fields for pass/fail, duration, and evidence path.

### Step 2: Run Fresh Host Verification

Configure outside the repository build directories with `SVM_BUILD_WIN32_APP=OFF`, build all host targets, run focused session/queue/RTU/command/boundary tests, then run the complete host CTest suite.

### Step 3: Run Full MinGW Verification

Run `scripts/build-windows-native-mingw.sh`, then build the entire `build-windows-native-mingw` tree with `--parallel 1` so every cross-test executable exists before invoking cross CTest.

### Step 4: Run the PTY Matrix

Execute `normal,reopen,timeout,cancel,stress,close,reopen-generation-isolation` with a recorded summary file. Require `gate-status=passed`, `GateStatus=passed`, `Classification=local-only-release-candidate-evidence`, and `Transport=serial-adapter-contract` or its consistently updated v2 replacement. Keep true stale-completion rejection in the deterministic session/queue tests.

### Step 5: Run Strict Wine and Package Verification

Run the MinGW package script with strict self-test and UI-performance behavior. Record executable results, package audit output, size limits, PE imports, forbidden runtime checks, package file set, and SHA256 sidecar.

### Step 6: Run Local UI Evidence When UI Paths Changed

Run the Wine UI capture workflow script and record self-test, UI-performance, screenshot/evidence, and gate-status outputs; do not treat screenshots as transport correctness evidence.

### Step 7: Run Static Repository Gates

Run the transport boundary checker, documentation consistency checker, relevant forbidden-symbol searches, and `git diff --check`. Verify no `SerialTransport`, `Win32SerialPort`, Qt runtime, TCP/UDP implementation, or Gray-code implementation leaked into scope.

### Step 8: Confirm Windows CI Evidence

Trigger or inspect the final `windows-native-package.yml` and `windows-native-ui-capture.yml` runs. Record run IDs, conclusions, CTest logs, package/UI artifact names, and key gate summaries.

### Step 9: Record Known Limitations

State that PTY/Wine is local release-candidate evidence, real PLC/USB-serial smoke testing is optional, and the one-second cancellation target is verified only for covered deterministic/native environments.

### Step 10: Close Workflow State

Mark the workflow complete only after every mandatory verification passes and the completion report contains reproducible commands and evidence. Leave any failed gate active and unresolved rather than reporting completion.

## Verification

- [x] Fresh host focused and full CTest runs pass.
- [x] Full MinGW build and cross CTest pass.
- [x] PTY matrix passes all required scenarios and summary assertions.
- [x] Wine self-test/UI performance and package audit pass.
- [x] Package size/import/forbidden-runtime/file-set/SHA256 checks pass.
- [x] Boundary, docs, forbidden-symbol, and diff checks pass.
- [x] Required GitHub Actions runs pass and their evidence is recorded.
- [x] Completion report distinguishes automated evidence from optional hardware coverage.

**Test command:**

```bash
cmake -S . -B /tmp/svm-transport-v2-final -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-final --parallel
ctest --test-dir /tmp/svm-transport-v2-final --output-on-failure
scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress,close,reopen-generation-isolation SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/serial-pty-matrix-summary.txt python3 scripts/run-windows-native-serial-pty-loopback.py
bash scripts/package-windows-native-mingw.sh
python3 scripts/check-transport-boundaries.py
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Expected output:**

```text
All host and MinGW CTest suites report 100% tests passed.
PTY matrix and package summaries report gate status passed.
Wine self-test and UI-performance checks pass in strict mode.
Package audit reports valid size, imports, runtime set, file set, and SHA256.
Boundary, documentation, and diff checks exit 0.
The completion report contains final Windows CI run evidence and known limitations.
```

## Commit

```text
chore: record serial transport v2 release verification (Phase 4, Task 03)
```
