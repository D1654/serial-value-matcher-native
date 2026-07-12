# Task 02: Guarantee Exactly-Once Completion

> Phase: 3 — Production Hardening
> Status: pending

---

## Objective

Settle every accepted request exactly once across sent, failed, timeout, cancellation, disconnect, close, and reconnect paths while meeting the approved deterministic terminal-result target.

## Files

**Create:**
- None

**Modify:**
- `src/win32/win32_serial_session.h`
- `src/win32/win32_serial_session.cpp`
- `tests/native_win32_serial_tests.cpp`
- `tests/native_reconnect_state_tests.cpp`

**Test:**
- `tests/native_win32_serial_tests.cpp`
- `tests/native_reconnect_state_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Windows API | `microsoft/Windows-classic-samples` | `CreateFileW`, `ReadFile`, `WriteFile`, `SetCommTimeouts`, `CancelIoEx`, `GetOverlappedResult`, `GetLastError` | Define the synchronous cancellation/close join boundary and retain an isolated future overlapped-I/O seam. |
| CMake/CTest | `Kitware/CMake` | `add_test`, `ctest --test-dir`, `--output-on-failure` | Run deterministic lifecycle and reconnect tests in host and cross builds. |
| Wine | `wine-mirror/wine` | `wine`, `wineboot`, `WINEPREFIX`, process exit codes | Execute Windows lifecycle tests and release self-test/performance gates. |
| GitHub Actions | `actions/runner` | Windows process invocation, `$env:SVM_NATIVE_SELF_TEST_LOG`, artifact logs | Preserve CI evidence for the same executable that passed CTest. |

## Steps

### Step 1: Enumerate request states

Document the accepted, active, terminal, and stale states and forbid transitions out of a terminal state.

### Step 2: Add session generation

Assign a monotonic generation to every successful open and copy it into every accepted request and completion record.

### Step 3: Invalidate before reconnect

Invalidate the old generation before closing its handle and before publishing the replacement session as open.

### Step 4: Settle queued work

Return `Cancelled` or `Disconnected` results for every queued request during close/reconnect and preserve request ID and byte count.

### Step 5: Settle active work

Join or deterministically terminate the active synchronous operation before releasing the handle, with a bounded cancellation/deadline path and no buffer reuse before completion.

### Step 6: Guard finalization

Add an exactly-once finalization guard so timeout, cancellation, I/O failure, close, and worker shutdown cannot publish duplicate results.

### Step 7: Reject stale completion

Discard or mark stale any completion carrying an older generation and prevent it from updating the new session's UI, counters, queue, or evidence.

### Step 8: Preserve no replay

Ensure reconnect never re-enqueues an old request automatically; callers must explicitly submit a new request after the new generation is open.

### Step 9: Define deadline behavior

Use a monotonic deadline for each accepted operation and classify expiration separately from caller cancellation and disconnect.

### Step 10: Map close outcomes

Return deterministic `Closed`/`Disconnected` or the existing equivalent typed status for work interrupted by close, without using localized messages as control flow.

### Step 11: Add lifecycle tests

Test accepted-once, sent-once, timeout-once, cancel-once, close-once, reconnect-once, stale-generation, and no-replay behavior with repeated completion polling.

### Step 12: Test the latency target

Measure the fake/session cancellation and disconnect paths against the approved `<= 1 second` target and record driver-dependent limitations without claiming unverified hardware behavior.

### Step 13: Verify handle ownership

Assert that only the session worker performs open, close, read, write, DTR, RTS, cancellation, and handle release operations.

## Verification

- [ ] Every accepted request produces one and only one terminal result.
- [ ] Close/reconnect invalidates the old generation before the new session publishes results.
- [ ] No old request is automatically replayed.
- [ ] Cancellation/disconnect tests meet the deterministic target in the fake/session path.
- [ ] Existing Windows serial and reconnect behavior remains available.

**Focused command:**
```bash
cmake -S . -B /tmp/svm-transport-v2-cmake -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-cmake --parallel
ctest --test-dir /tmp/svm-transport-v2-cmake --output-on-failure -R "native_reconnect_state|native_serial_io_state|transport_contract"
```

**Focused expected evidence:**
```
100% tests passed
```

**MinGW command:**
```bash
scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure -R "native_win32_serial|native_reconnect_state|native_serial_io_state|transport_contract"
```

**MinGW expected evidence:**
```
100% tests passed
```

**PTY command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/task-02-serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

**PTY expected evidence:**
```
python serial scenario ok scenario=reopen
python serial scenario ok scenario=cancel
python serial matrix summary gate-status=passed ...
GateStatus=passed
```

**Wine command:**
```bash
mkdir -p /tmp/svm-v2-task-02-xdg
chmod 700 /tmp/svm-v2-task-02-xdg
env WINEPREFIX=/tmp/svm-v2-task-02-wine WINEARCH=win64 XDG_RUNTIME_DIR=/tmp/svm-v2-task-02-xdg \
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
SVM_MINGW_PACKAGE_DIR=artifacts/windows-native-mingw-task-02 \
SVM_WINEPREFIX=/tmp/svm-v2-task-02-wine \
scripts/package-windows-native-mingw.sh
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Package expected evidence:**
```
Gate status: passed
Native exe present: yes
Zip sha256 file matches: yes
Forbidden Qt/SQLite/.NET runtime files: none
docs consistency ok
```

## Commit

```
feat: guarantee exactly-once serial completion (Phase 3, Task 02)
```
