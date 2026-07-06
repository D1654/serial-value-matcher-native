# Task 03: Add Declarative Command Sequence Foundation

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

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

- [ ] Command sequence is declarative and local.
- [ ] No scripting engine or arbitrary code execution is introduced.
- [ ] Tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "command_sequence|serial_write_queue|modbus_scan_executor"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
feat: add declarative command sequence foundation (Phase 3, Task 03)
```
