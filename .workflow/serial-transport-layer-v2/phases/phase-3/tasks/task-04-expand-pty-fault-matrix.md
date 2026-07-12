# Task 04: Expand PTY Fault Matrix

> Phase: 3 — Production Hardening
> Status: pending

---

## Objective

Make close, cancellation, timeout, reopen, stale-generation, stress, and no-port behavior reproducible through the native loopback test and Linux/Wine PTY harness while preserving local-only release-candidate semantics.

## Files

**Create:**
- None

**Modify:**
- `tests/native_win32_serial_loopback_tests.cpp`
- `tests/native_win32_serial_tests.cpp`
- `scripts/run-windows-native-serial-pty-loopback.py`

**Test:**
- `tests/native_win32_serial_loopback_tests.cpp`
- `tests/native_win32_serial_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Windows API | `microsoft/Windows-classic-samples` | `CreateFileW`, `ReadFile`, `WriteFile`, `SetCommTimeouts`, `ClearCommError`, `CancelIoEx` | Exercise native serial open/read/write/timeout/cancel behavior through the session contract. |
| CMake/CTest | `Kitware/CMake` | `add_test`, `ctest --test-dir`, cross-compiling emulator | Build and run the native loopback target and no-port safety path. |
| Wine | `wine-mirror/wine` | `wineboot`, `wine`, `WINEPREFIX`, `dosdevices` COM symlinks | Connect the Windows loopback executable to a POSIX PTY and run fault scenarios. |
| GitHub Actions | `actions/runner` | Artifact logs and local-only boundary notes | Keep CI honest: upload the PTY boundary note without claiming Windows runners execute POSIX PTYs. |

## Steps

### Step 1: Preserve the request/response fixture

Keep the existing deterministic Modbus request and response bytes as the normal-path oracle.

### Step 2: Add close interruption

Add a scenario that observes an in-flight request, closes the session, and requires one cancellation/disconnect terminal result with no late response accepted.

### Step 3: Expand reopen checks

Repeat open/close/reopen cycles and verify each new generation exchanges only its own request/response.

### Step 4: Add stale-generation assertions

Delay or retain an old completion and assert that it cannot update the replacement session or satisfy its response wait.

### Step 5: Keep timeout deterministic

Withhold the response and assert the configured timeout status, bounded process completion, and zero successful transactions.

### Step 6: Keep cancel deterministic

Observe the request, withhold its response, and assert cancellation within the configured wait without manufacturing a response.

### Step 7: Exercise queue pressure

Submit enough bounded writes to hit request and byte limits and require immediate rejection without process hang or dropped older work.

### Step 8: Retain stress coverage

Run the stress scenario with explicit iteration/reopen environment values and verify FIFO request/response pairing for every transaction.

### Step 9: Preserve no-port safety

Run the loopback executable without a COM endpoint and require a safe skip or pass, never a crash or indefinite wait.

### Step 10: Extend harness controls

Add validated environment controls for the new fault timings and scenario selection while retaining `normal,reopen,timeout,cancel,stress` defaults and bounded integer validation.

### Step 11: Preserve summary semantics

Continue writing `GateStatus=passed`, `Classification=local-only-release-candidate-evidence`, scenario names, transaction count, executable, COM/PTTY, and transport contract evidence.

### Step 12: Verify failure reporting

Make unexpected request bytes, process timeout, missing endpoint, and nonzero child exit report the scenario and transaction context before cleanup.

### Step 13: Document CI boundary in output

Keep the harness and summary explicit that Windows GitHub Actions does not execute POSIX PTY scenarios; do not promote local evidence to a CI claim.

## Verification

- [ ] Focused native loopback/no-port and serial parameter tests pass.
- [ ] MinGW builds every test executable before CTest; no-port safety remains green.
- [ ] PTY normal/reopen/timeout/cancel/stress and new close/stale-generation paths pass.
- [ ] Wine self-test/UI performance and package gates remain green.
- [ ] Summary remains machine-readable and correctly classified as local-only.

**Focused command:**
```bash
cmake -S . -B /tmp/svm-transport-v2-cmake -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-cmake --parallel
ctest --test-dir /tmp/svm-transport-v2-cmake --output-on-failure -R "native_reconnect_state|native_serial_io_state"
```

**Focused expected evidence:**
```
100% tests passed
```

**MinGW command:**
```bash
scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure -R "native_win32_serial_tests|native_win32_serial_loopback_tests|native_reconnect_state"
```

**MinGW expected evidence:**
```
100% tests passed
native_win32_serial_loopback_tests ... passed or safely skipped without a port
```

**PTY command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress \
SVM_SERIAL_LOOPBACK_REOPEN_COUNT=3 \
SVM_SERIAL_LOOPBACK_STRESS_ITERATIONS=5000 \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/task-04-serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

**PTY expected evidence:**
```
python serial scenario ok scenario=normal
python serial scenario ok scenario=reopen
python serial scenario ok scenario=timeout
python serial scenario ok scenario=cancel
python serial scenario ok scenario=stress
python serial matrix summary gate-status=passed ...
GateStatus=passed
Classification=local-only-release-candidate-evidence
```

**Wine command:**
```bash
mkdir -p /tmp/svm-v2-task-04-xdg
chmod 700 /tmp/svm-v2-task-04-xdg
env WINEPREFIX=/tmp/svm-v2-task-04-wine WINEARCH=win64 XDG_RUNTIME_DIR=/tmp/svm-v2-task-04-xdg \
xvfb-run -a bash -c 'wine build-windows-native-mingw/svm-native-win32.exe --self-test && wine build-windows-native-mingw/svm-native-win32.exe --ui-perf-test'
```

**Wine expected evidence:**
```
ok
ui-perf ok
```

**Package and documentation command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_MINGW_PACKAGE_DIR=artifacts/windows-native-mingw-task-04 \
SVM_WINEPREFIX=/tmp/svm-v2-task-04-wine \
scripts/package-windows-native-mingw.sh
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Package expected evidence:**
```
Gate status: passed
Serial PTY summary: GateStatus=passed
Zip sha256 file matches: yes
Unexpected DLL files: none
docs consistency ok
```

## Commit

```
test: expand serial PTY fault matrix (Phase 3, Task 04)
```
