# Task 03: Add Declarative Command Sequence Foundation

> Phase: 3 — Extension Capability & Production Hardening
> Status: completed

---

## Objective

Add a local declarative command sequence and assertion foundation without introducing a general script engine.

## Files

**Create:**
- `src/command_sequence/command_sequence.h`
- `src/command_sequence/command_sequence.cpp`
- `tests/command_sequence_tests.cpp`

**Modify:**
- `src/transport/serial_write_queue.h`
- `src/modbus/modbus_scan_executor.h`
- `src/capture/session_evidence.h`
- `CMakeLists.txt`

**Test:**
- `tests/command_sequence_tests.cpp`
- Serial queue and Modbus executor tests

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| CMake | Kitware/CMake | `add_executable`, `add_test` | Register command-sequence tests. |

## Steps

### Step 1: Define Command Model

Add declarative commands for serial write, delay, wait-for-response, Modbus read, and assertion.

### Step 2: Define Safety Limits

Add maximum sequence length, timeout policy, cancellation, and no arbitrary code execution.

### Step 3: Integrate Existing Backends

Use serial write queue and Modbus executor abstractions; do not create a separate send path.

### Step 4: Capture Evidence

Record command execution and assertion results in session evidence.

### Step 5: Add Tests

Cover valid sequence, timeout, cancellation, assertion failure, and unsafe command rejection.

## Verification

- [x] Command sequence is declarative and local.
- [x] No scripting engine or arbitrary code execution is introduced.
- [x] Tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "command_sequence|serial_write_queue|modbus_scan_executor"
```

**Expected output:**
```
100% tests passed
```

**Completed verification — 2026-07-09T17:56:17+08:00:**

- `cmake --build build-codex --target command_sequence_tests serial_write_queue_tests modbus_scan_executor_tests session_evidence_tests -j 2` passed.
- `ctest --test-dir build-codex --output-on-failure -R "command_sequence|serial_write_queue|modbus_scan_executor|session_evidence"` passed, 4/4.
- `cmake --build build-windows-native-mingw --target command_sequence_tests serial_write_queue_tests native_protocol_modbus_tests svm-native-win32 -j 2` passed.
- `ctest --test-dir build-windows-native-mingw --output-on-failure -R "command_sequence|serial_write_queue|native_protocol_modbus"` passed, 3/3.
- `cmake --build build-codex -j 2` passed.
- `ctest --test-dir build-codex --output-on-failure` passed, 53/53.
- `cmake --build build-windows-native-mingw -j 2` passed.
- `ctest --test-dir build-windows-native-mingw --output-on-failure` passed, 30/30.
- `python3 scripts/check-docs-artifact-consistency.py` passed.
- `git diff --check` passed.
- `bash scripts/package-windows-native-mingw.sh` passed; zip `875132` bytes, extracted `2185500` bytes, SHA256 `86f39c2c46e89f8974fdb1ea56b11ff92c095cdb07ddd52998e02d4cbae1f349`.

## Commit

```
feat: add declarative command sequence foundation (Phase 3, Task 03)
```
