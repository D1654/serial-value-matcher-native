# Task 04: Add Dangerous Operation Confirmation and Audit

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

---

## Objective

Require explicit confirmation and audit evidence for dangerous writes, batch commands, and broadcast Modbus writes.

## Files

**Create:**
- `src/core/dangerous_operation_policy.h`
- `src/core/dangerous_operation_policy.cpp`
- `tests/dangerous_operation_policy_tests.cpp`

**Modify:**
- `src/win32/main_window_send.cpp`
- `src/win32/main_window_modbus.cpp`
- `src/capture/session_evidence.h`
- `src/capture/session_evidence.cpp`
- `CMakeLists.txt`

**Test:**
- `tests/dangerous_operation_policy_tests.cpp`
- Relevant send/Modbus tests

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | dialog/message box references | Native explicit confirmation UI. |

## Steps

### Step 1: Define Dangerous Operation Rules

Classify batch write, broadcast Modbus write, dangerous register write, and command-sequence write operations.

### Step 2: Add Policy Tests

Test which operations require confirmation and which are safe.

### Step 3: Add UI Confirmation

Prompt before dangerous operation execution using local native UI.

### Step 4: Record Audit Evidence

Record confirmation result, operation summary, timestamp, and redaction-safe details.

### Step 5: Verify No Silent Execution

Ensure cancelled dangerous operations do not enqueue writes or execute Modbus commands.

## Verification

- [ ] Dangerous operations require explicit confirmation.
- [ ] Cancelled operations do not execute.
- [ ] Confirmation/cancellation evidence is recorded.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "dangerous_operation_policy|native_send|modbus"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
feat: add dangerous operation confirmation audit (Phase 3, Task 04)
```
