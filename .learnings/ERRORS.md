# Errors

Command failures and integration errors.

---

## [ERR-20260602-001] cmake_qt_parallel_build_sigkill

**Logged**: 2026-06-02T09:05:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: infra

### Summary
Qt/CMake native project build was killed by SIGKILL during default parallel build on the low-memory 1-core Debian VM; rerunning with `--parallel 1` succeeded.

### Error
```text
cmake --build build
Process exited with signal SIGKILL
```

### Context
- Project: `projects/serial-value-matcher-native`
- Operation: adding T04C reconnect policy/tests and rebuilding Qt targets.
- Environment: Debian 12 Hyper-V VM, about 3.8 GiB RAM, 1 visible vCPU.
- The build had already linked `svm-native`; failure occurred while compiling/linking test targets.

### Suggested Fix
For this project on the current VM, prefer `cmake --build build --parallel 1` after adding Qt/MOC-heavy targets. Do not treat a default parallel-build SIGKILL as code failure until the serial build has been tried.

### Metadata
- Reproducible: yes
- Related Files: CMakeLists.txt
- See Also: workspace memory warning about not treating SIGKILL as passing/failing code verification

### Resolution
- **Resolved**: 2026-06-02T09:06:00+08:00
- **Notes**: `cmake --build build --parallel 1` succeeded, then `ctest --test-dir build --output-on-failure` passed 9/9.

---

## [ERR-20260603-001] missing_xxd_command

**Logged**: 2026-06-03T08:55:00+08:00
**Priority**: low
**Status**: resolved
**Area**: infra

### Summary
Attempted to inspect Git commit-message bytes with `xxd`, but the current Debian VM does not have `xxd` installed.

### Error
```text
/bin/bash: 行 1: xxd: 未找到命令
```

### Context
- Project: `projects/serial-value-matcher-native`
- Operation: checking whether a Chinese Git commit message was stored as UTF-8 after Git emitted an encoding warning.
- Environment: Debian 12 VM.

### Suggested Fix
Use built-in/core utilities such as `od -An -tx1` or Python byte inspection instead of assuming `xxd` exists.

### Metadata
- Reproducible: yes
- Related Files: workspace `TOOLS.md` tooling snapshot

### Resolution
- **Resolved**: 2026-06-03T08:56:00+08:00
- **Notes**: Switched to other byte-inspection tools; no project build impact.

---

## [ERR-20260603-002] qt_timezone_utc_constant_not_available

**Logged**: 2026-06-03T10:25:00+08:00
**Priority**: low
**Status**: resolved
**Area**: tests

### Summary
T05E test target failed to compile because the installed Qt 6.4 headers do not provide the `QTimeZone::UTC` constant used in the first test draft.

### Error
```text
error: ‘UTC’ is not a member of ‘QTimeZone’
```

### Context
- Project: `projects/serial-value-matcher-native`
- Operation: adding Modbus fake transport scan executor tests.
- Environment: Debian 12 VM with Qt 6.4.2.
- The production code was not at fault; the test helper used a Qt API form not available in this environment.

### Suggested Fix
Use portable Qt time construction for tests, e.g. `QDateTime::currentDateTimeUtc()` or `QDateTime(..., Qt::UTC)`, instead of assuming `QTimeZone::UTC` exists.

### Metadata
- Reproducible: yes
- Related Files: `tests/modbus_scan_executor_tests.cpp`

### Resolution
- **Resolved**: 2026-06-03T10:27:00+08:00
- **Notes**: Replaced the incompatible test time construction and reran validation; `modbus_scan_executor_tests` passed and full `ctest` passed 14/14.

---

## [ERR-20260603-003] qt_sqlite_null_qstring_not_null_constraint

**Logged**: 2026-06-03T12:55:00+08:00
**Priority**: low
**Status**: resolved
**Area**: storage/tests

### Summary
T05F scan persistence tests failed because default-constructed `QString` values were bound as SQL NULL and violated NOT NULL constraints for text fields.

### Error
```text
NOT NULL constraint failed: scan_sessions.error_message
NOT NULL constraint failed: scan_attempts.error_message
```

### Context
- Project: `projects/serial-value-matcher-native`
- Operation: adding `scan_sessions`, `scan_attempts`, and `scan_observations` persistence.
- Empty diagnostic fields such as successful session/attempt `error_message` were represented by default/null `QString` values.

### Suggested Fix
Before binding text fields that are declared `NOT NULL`, normalize nullable `QString` values to explicit empty strings, e.g. with a helper like `notNullString()`.

### Metadata
- Reproducible: yes
- Related Files: `src/storage/session_store.cpp`, `tests/modbus_scan_persistence_tests.cpp`

### Resolution
- **Resolved**: 2026-06-03T12:57:00+08:00
- **Notes**: Added `notNullString()` and used explicit empty strings for NOT NULL text fields; targeted persistence test and full `ctest` passed 15/15.

---

## [ERR-20260603-004] stale_ninja_cc1plus_processes_caused_repeated_sigkill

**Logged**: 2026-06-03T17:30:00+08:00
**Priority**: medium
**Status**: resolved
**Area**: build/infra

### Summary
T05G validation repeatedly hit SIGKILL even with `--parallel 1` because older interrupted Ninja builds left concurrent `ninja`, `c++`, and `cc1plus` processes running in the same build directory.

### Error
```text
Process exited with signal SIGKILL
```

### Context
- Project: `projects/serial-value-matcher-native`
- Operation: building `modbus_rtu_serial_transport_tests` after adding the serial transport adapter.
- Environment: low-memory Debian VM.
- Multiple stale compiler processes made the effective build parallel despite passing `-j1` / `--parallel 1`.

### Suggested Fix
Before retrying after a SIGKILL, check and clear stale build processes for the project (`ninja`, `c++`, `cc1plus`), then use a clean build directory and explicit `ninja -j1`. Avoid diagnostic commands such as `ninja -d explain <target>` without `-j1`, because they can trigger a default parallel build.

### Metadata
- Reproducible: yes
- Related Files: `CMakeLists.txt`

### Resolution
- **Resolved**: 2026-06-03T17:36:00+08:00
- **Notes**: Killed stale build processes, configured clean `build-t05g`, then ran single-thread target/full validation; `ctest` passed 16/16.

---

## [ERR-20260605-001] cmake_build

**Logged**: 2026-06-05T13:55:00+08:00
**Priority**: medium
**Status**: pending
**Area**: infra

### Summary
SerialValueMatcher Native 构建在 `cmake --build build --parallel 2` 接近末尾时被 SIGKILL。

### Error
```text
Process exited with signal SIGKILL after linking most targets near [51/53]
```

### Context
- Command: `cmake --build build --parallel 2`
- Environment: Debian 12 Hyper-V VM, low-memory profile
- Build had already compiled and linked most targets; no C++ compiler diagnostic appeared before SIGKILL.

### Suggested Fix
Retry with `cmake --build build --parallel 1` and avoid concurrent heavy builds on this VM.

### Metadata
- Reproducible: unknown
- Related Files: projects/serial-value-matcher-native/CMakeLists.txt

---

## [ERR-20260606-001] Qt lambda capture omitted after adding interpretation-map validation

**Logged**: 2026-06-06T11:01:40+08:00
**Area**: ui | cxx | build

### Summary
T12-A build failed because `validateInterpretationMapBeforeSave` was declared in `showAnalysisWorkspace()` but not captured by two Qt signal lambdas.

### Details
After adding save-time interpretation-map validation, `confirmRuleButton` and `editRuleButton` callbacks called the local validation lambda but their capture lists did not include it. The compiler stopped in `main_window.cpp` with “is not captured”.

### Suggested Action
When introducing local helper lambdas used inside nested Qt callbacks, update every callback capture list or move the helper to a private/static function if it will be reused widely.

## 2026-06-06 — Qt lambda capture omitted executeButton in Modbus scan UI

- Context: Phase 2 Task 00 Modbus scan thin UI, `MainWindow::showModbusScanDialog()`.
- Symptom: `cmake --build build --parallel 1` failed because the clicked lambda called `executeButton->setEnabled(...)` but did not capture `executeButton`.
- Fix: Add `executeButton` to the lambda capture list.
- Lesson: When connecting Qt lambdas that use local widget pointers, explicitly audit every local pointer used inside the lambda body and capture it; this repeats the earlier T12-A lambda capture failure pattern.
