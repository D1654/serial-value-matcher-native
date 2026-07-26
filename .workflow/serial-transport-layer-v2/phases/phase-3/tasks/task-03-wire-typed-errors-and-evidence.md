# Task 03: Wire Typed Errors And Evidence

> Phase: 3 — Production Hardening
> Status: Completed

---

## Objective

Expose structured operation/error evidence to UI logs and native records while keeping localization at the UI boundary and payload logging opt-in.

## Files

**Create:**
- None

**Modify:**
- `src/transport/serial_types.h`
- `src/win32/win32_serial_session.cpp`
- `src/win32/main_window_serial.cpp`
- `src/win32/main_window_serial_io.cpp`
- `src/win32/main_window_log.cpp`
- `src/win32/native_log_model.h`
- `tests/transport_contract_tests.cpp`

**Test:**
- `tests/transport_contract_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Windows API | `microsoft/Windows-classic-samples` | `GetLastError`, `FormatMessageW`, `ClearCommError`, `COMSTAT`, `SetCommTimeouts` | Preserve native error codes/queue diagnostics in structured evidence while deferring localized text to Win32 UI code. |
| CMake/CTest | `Kitware/CMake` | `add_test`, CTest regex selection, `--output-on-failure` | Run contract and log/evidence regressions without requiring a device. |
| Wine | `wine-mirror/wine` | `wine`, `WINEPREFIX`, `xvfb-run` | Verify the localized UI executable still emits successful self-test/performance evidence. |
| GitHub Actions | `actions/runner` | `$env:SVM_NATIVE_SELF_TEST_LOG`, `GITHUB_STEP_SUMMARY`, artifact upload | Preserve machine-readable logs and evidence files in CI. |

## Steps

### Step 1: Define typed status fields

Add neutral operation, direction, lifecycle, timeout, cancellation, disconnect, and native-error categories without adding UI strings to transport types.

### Step 2: Add operation identity

Carry request ID, session generation, endpoint, deadline, byte count, and terminal status in every accepted-operation result.

### Step 3: Preserve native diagnostics

Record stable Win32 error code and operation context at the failure site before any later call can overwrite `GetLastError()`.

### Step 4: Separate localization

Map typed category/code to Chinese UI text in `main_window_serial.cpp`; prohibit downstream branching on localized error text.

### Step 5: Update receive evidence

Make `main_window_serial_io.cpp` attach operation/session metadata to RX/TX state and raw-event persistence while retaining the current raw payload behavior.

### Step 6: Keep payloads private by default

Emit metadata-only transport events by default and preserve raw payload output only through the existing explicit evidence/log path.

### Step 7: Extend log model metadata

Add bounded fields for direction, endpoint, request ID, generation, status, byte count, deadline/elapsed time, and native code without exposing a HANDLE or Win32 type in the model.

### Step 8: Update log rendering

Render concise localized status and diagnostic context in `main_window_log.cpp` without duplicating transport classification logic.

### Step 9: Preserve filtering/export

Ensure the new metadata does not break current visible-log filtering, copying, exporting, timestamp display, or native raw-event storage.

### Step 10: Test typed branching

Use the fake contract to verify timeout, cancellation, disconnect, short write, native error code, generation, request ID, and no-default-payload evidence semantics.

### Step 11: Test localization isolation

Assert that transport decisions remain correct if the UI text changes and that no caller searches for a localized substring to classify an error.

### Step 12: Test bounded evidence

Exercise long endpoint/error text and repeated events to confirm metadata remains bounded and log flush behavior remains stable.

## Verification

- [x] Transport callers branch on typed status/category, not localized strings.
- [x] Operation evidence includes request ID, generation, endpoint, deadline/result, byte count, and native code when available.
- [x] Default transport evidence does not include payload bytes.
- [x] Existing raw TX/RX, visible log, filtering, export, self-test, and UI perf behavior remains intact.

**Focused command:**
```bash
cmake -S . -B /tmp/svm-transport-v2-cmake -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-cmake --parallel
ctest --test-dir /tmp/svm-transport-v2-cmake --output-on-failure -R "transport_contract|native_serial_io_state|evidence_bundle_writer|native_status_counters"
```

**Focused expected evidence:**
```
100% tests passed
```

**MinGW command:**
```bash
scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure -R "transport_contract|native_win32_serial|native_serial_io_state|native_status_counters"
```

**MinGW expected evidence:**
```
100% tests passed
```

**PTY command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/task-03-serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

**PTY expected evidence:**
```
python serial matrix ok ...
GateStatus=passed
Transport=serial-adapter-contract
```

**Wine command:**
```bash
mkdir -p /tmp/svm-v2-task-03-xdg
chmod 700 /tmp/svm-v2-task-03-xdg
env WINEPREFIX=/tmp/svm-v2-task-03-wine WINEARCH=win64 XDG_RUNTIME_DIR=/tmp/svm-v2-task-03-xdg \
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
SVM_MINGW_PACKAGE_DIR=artifacts/windows-native-mingw-task-03 \
SVM_WINEPREFIX=/tmp/svm-v2-task-03-wine \
scripts/package-windows-native-mingw.sh
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Package expected evidence:**
```
Gate status: passed
Unicode text probe: passed
Package documentation links: passed
Package documentation file set: passed
docs consistency ok
```

## Commit

```
feat: wire typed serial errors and evidence (Phase 3, Task 03)
```
