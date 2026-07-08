# Task 04: Unify Native Modbus Worker Adapter

> Phase: 2 — Backend Consistency
> Status: completed

---

## Objective

Make native Modbus worker delegate protocol behavior to the shared executor and transport abstractions.

## Files

**Create:**
- None

**Modify:**
- `src/win32/native_modbus_scan_worker.h`
- `src/win32/native_modbus_scan_worker.cpp`
- `src/modbus/modbus_scan_executor.h`
- `src/modbus/modbus_scan_executor.cpp`
- `src/modbus/modbus_rtu_serial_transport.h`
- `src/modbus/modbus_rtu_serial_transport.cpp`
- `tests/modbus_scan_executor_tests.cpp`
- `tests/modbus_rtu_serial_transport_tests.cpp`

**Test:**
- `tests/modbus_scan_executor_tests.cpp`
- `tests/modbus_rtu_serial_transport_tests.cpp`
- `tests/native_modbus_scan_request_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Modbus executor uses existing internal protocol code. |

## Steps

### Step 1: Compare Worker and Executor Behavior

Identify duplicated timeout, retry, exception, request, and response handling.

### Step 2: Keep Worker as Thread Adapter

Move protocol decisions into executor/transport path while worker manages scheduling and progress notifications.

### Step 3: Preserve UI Progress Events

Ensure UI scan progress and cancellation behavior remain unchanged.

### Step 4: Add Executor Tests

Cover worker-to-executor delegation and transport error propagation.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "modbus_scan_executor|modbus_rtu_serial_transport|native_modbus_scan_request"
```

## Verification

- [x] Worker no longer owns protocol interpretation.
- [x] Shared executor handles Modbus transaction behavior.
- [x] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "modbus_scan_executor|modbus_rtu_serial_transport|native_modbus_scan_request"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
refactor: unify native modbus worker executor adapter (Phase 2, Task 04)
```
