# Task 01: Integrate Queue Backpressure

> Phase: 3 — Production Hardening
> Status: pending

---

## Objective

Enforce the confirmed 64-request and 256 KiB byte budgets in the production session, including the active request, with immediate typed rejection and truthful UI snapshots.

## Files

**Create:**
- None

**Modify:**
- `src/win32/win32_serial_session.h`
- `src/win32/win32_serial_session.cpp`
- `src/win32/native_serial_io_state.h`
- `src/win32/native_serial_io_state.cpp`
- `src/win32/main_window_serial_io.cpp`
- `tests/native_serial_io_state_tests.cpp`

**Test:**
- `tests/native_serial_io_state_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Windows API | `microsoft/Windows-classic-samples` | `WriteFile`, `GetOverlappedResult`, `GetLastError`, `CancelIoEx` | Preserve the session's native write completion boundary while queue admission and accounting remain platform-neutral. |
| CMake/CTest | `Kitware/CMake` | `add_test`, `ctest --test-dir`, `-R` test selection | Build and run focused queue/session regressions in host and MinGW trees. |
| Wine | `wine-mirror/wine` | `wine`, `wineboot`, `WINEPREFIX`, COM `dosdevices` | Execute the Windows serial test binary and the existing PTY harness locally. |
| GitHub Actions | `actions/runner` | PowerShell environment variables, step summaries, artifact evidence | Preserve the package workflow's CI evidence and upload contract. |

## Steps

### Step 1: Inventory admission paths

Trace every production enqueue, dequeue, active-write, completion, cancellation, close, and reconnect transition in the session before changing counters.

### Step 2: Define hard limits

Record the request limit as `64` and the counted-byte limit as `256 KiB` in one neutral session policy value; do not duplicate literals in UI code.

### Step 3: Add byte accounting

Track pending bytes and the active request bytes with checked arithmetic so an overflow or over-budget payload is rejected before it is copied.

### Step 4: Define admission rules

Reject invalid payloads, count overflow, and byte-budget overflow immediately with the existing typed rejection status; never block, drop an older request, or silently split a manual payload.

### Step 5: Preserve FIFO ordering

Keep accepted requests in FIFO order and ensure a rejected request does not consume a request ID or alter the queue's existing order guarantees.

### Step 6: Account for active work

Move a request from pending to active without decrementing counted work, and release its count and bytes exactly once at a terminal result.

### Step 7: Expose queue snapshots

Extend the session snapshot with pending count, pending bytes, limits, active request ID, and high-water values needed by the UI without exposing the native handle.

### Step 8: Update serial state

Make `NativeSerialIoState` consume the snapshot and distinguish accepted, sent, rejected-full, cancelled, timeout, and disconnected outcomes without parsing localized text.

### Step 9: Update UI polling

Update `main_window_serial_io.cpp` to render queue pressure from the snapshot and keep receive polling independent of queue admission.

### Step 10: Test byte boundaries

Add tests for exact-limit acceptance, one-byte-over-limit rejection, count-plus-active accounting, zero/oversized payload rejection, and counter release after every terminal status.

### Step 11: Test snapshot stability

Verify snapshots remain coherent during enqueue, active completion, cancellation, and close, including an empty queue after all terminal results are drained.

### Step 12: Review compatibility behavior

Confirm manual, timed, and file-send callers still receive immediate typed backpressure and that file chunking remains the only way to send work larger than the byte budget.

## Verification

- [ ] Focused host tests cover count/byte admission, active accounting, queue snapshots, and terminal release.
- [ ] MinGW tests cover the same contract through the Windows session target.
- [ ] PTY matrix remains green for normal, reopen, timeout, cancel, and stress.
- [ ] Wine self-test and UI performance logs remain successful.
- [ ] Package audit, size/security gates, docs consistency, and diff checks remain successful.

**Focused command:**
```bash
cmake -S . -B /tmp/svm-transport-v2-cmake -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-cmake --parallel
ctest --test-dir /tmp/svm-transport-v2-cmake --output-on-failure -R "serial_write_queue|transport_contract|native_serial_io_state"
```

**Focused expected evidence:**
```
100% tests passed
```

**MinGW command:**
```bash
scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure -R "serial_write_queue|transport_contract|native_serial_io_state|native_win32_serial"
```

**MinGW expected evidence:**
```
100% tests passed
```

**PTY command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/task-01-serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

**PTY expected evidence:**
```
python serial matrix ok ... scenarios=normal,reopen,timeout,cancel,stress
GateStatus=passed
Classification=local-only-release-candidate-evidence
Transport=serial-adapter-contract
```

**Wine command:**
```bash
mkdir -p /tmp/svm-v2-task-01-xdg
chmod 700 /tmp/svm-v2-task-01-xdg
env WINEPREFIX=/tmp/svm-v2-task-01-wine WINEARCH=win64 XDG_RUNTIME_DIR=/tmp/svm-v2-task-01-xdg \
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
SVM_MINGW_PACKAGE_DIR=artifacts/windows-native-mingw-task-01 \
SVM_WINEPREFIX=/tmp/svm-v2-task-01-wine \
scripts/package-windows-native-mingw.sh
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Package expected evidence:**
```
Gate status: passed
Zip sha256 file matches: yes
Unexpected DLL files: none
Required package files: passed
Package documentation file set: passed
docs consistency ok
```

## Commit

```
feat: enforce serial session queue backpressure (Phase 3, Task 01)
```
