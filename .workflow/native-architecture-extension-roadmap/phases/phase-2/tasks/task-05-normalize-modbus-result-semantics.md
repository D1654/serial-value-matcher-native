# Task 05: Normalize Modbus Result Semantics

> Phase: 2 — Backend Consistency
> Status: pending

---

## Objective

Normalize Modbus timeout, retry, exception, CRC/length, and data-format results across native UI and core executor paths.

## Files

**Create:**
- None

**Modify:**
- `src/modbus/modbus_read_response.h`
- `src/modbus/modbus_read_response.cpp`
- `src/modbus/modbus_scan_plan.h`
- `src/modbus/modbus_scan_plan.cpp`
- `src/win32/native_modbus_scan_ui_state.h`
- `src/win32/native_modbus_scan_ui_state.cpp`
- `tests/modbus_read_response_tests.cpp`
- `tests/modbus_scan_plan_tests.cpp`
- `tests/native_modbus_scan_ui_state_tests.cpp`

**Test:**
- Modbus response, scan plan, UI state tests

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Internal Modbus result modeling only. |

## Steps

### Step 1: Define Result Taxonomy

Classify normal response, exception response, CRC error, malformed length, timeout, retry exhausted, cancel, and transport failure.

### Step 2: Update Core Result Types

Add or refine fields in Modbus response/plan/executor result structures.

### Step 3: Update UI State Mapping

Map normalized results to user-facing labels and status counters.

### Step 4: Add Matrix Tests

Cover all result categories and ensure UI mapping remains stable.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "modbus_read_response|modbus_scan_plan|native_modbus_scan_ui_state"
```

## Verification

- [ ] Every Modbus result category has one internal meaning.
- [ ] UI text/status mapping is consistent.
- [ ] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "modbus_read_response|modbus_scan_plan|native_modbus_scan_ui_state"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
refactor: normalize native modbus result semantics (Phase 2, Task 05)
```
