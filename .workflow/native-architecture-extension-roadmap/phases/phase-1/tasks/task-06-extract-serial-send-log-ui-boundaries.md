# Task 06: Extract Serial Send Log UI Boundaries

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Move serial, send, and log coordination toward small controller-style helpers while preserving existing HWND ownership and behavior.

## Files

**Create:**
- `src/win32/native_serial_send_controller.h`
- `src/win32/native_serial_send_controller.cpp`

**Modify:**
- `src/win32/main_window_serial.cpp`
- `src/win32/main_window_send.cpp`
- `src/win32/main_window_log.cpp`
- `src/win32/native_serial_io_state.h`
- `src/win32/native_serial_io_state.cpp`
- `src/win32/native_send_control_state.h`
- `src/win32/native_send_control_state.cpp`
- `src/win32/native_log_model.h`
- `src/win32/native_log_model.cpp`
- Related tests for serial/send/log state

**Test:**
- `tests/native_serial_io_state_tests.cpp`
- `tests/native_send_control_state_tests.cpp`
- `tests/native_log_filter_state_tests.cpp`
- `tests/native_send_history_state_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | message/command dispatch patterns | Preserve native UI command behavior while reducing coupling. |

## Steps

### Step 1: Identify Serial/Send/Log Coupling

List direct calls between serial state, send controls, log model, and main window command handlers.

### Step 2: Create Narrow Controller Helper

Create a small helper for state transitions and command decisions, not HWND ownership.

### Step 3: Route Command Decisions Through Helper

Move serial/send/log decision logic out of direct `NativeMainWindow` methods where safe.

### Step 4: Preserve UI Event Behavior

Ensure button state, input state, send history, log filtering, and error display remain unchanged.

### Step 5: Add or Update State Tests

Add tests for manual send control, log state, and serial UI state transitions.

### Step 6: Split During Phase 4 If Needed

If this task exceeds safe scope during implementation, split before coding further.

## Verification

- [ ] Serial/send/log behavior remains unchanged from user perspective.
- [ ] Controller helper owns decisions, not HWNDs.
- [ ] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "native_serial_io_state|native_send_control_state|native_log_filter_state|native_send_history_state"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
refactor: extract native serial send log boundaries (Phase 1, Task 06)
```
