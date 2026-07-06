# Task 07: Extract Modbus Analysis Preference UI Boundaries

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Clarify Modbus, analysis, and preference UI coordination before backend unification work begins.

## Files

**Create:**
- `src/win32/native_modbus_analysis_controller.h`
- `src/win32/native_modbus_analysis_controller.cpp`

**Modify:**
- `src/win32/main_window_modbus.cpp`
- `src/win32/main_window_analysis.cpp`
- `src/win32/main_window_preferences.cpp`
- `src/win32/native_modbus_scan_ui_state.h`
- `src/win32/native_modbus_scan_ui_state.cpp`
- `src/win32/native_ui_preferences.h`
- `src/win32/native_ui_preferences.cpp`
- Related tests for Modbus/analysis/preferences state

**Test:**
- `tests/native_modbus_scan_ui_state_tests.cpp`
- `tests/native_analysis_report_tests.cpp`
- `tests/native_ui_preferences_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | command/message dispatch patterns | Keep native UI behavior stable while preparing backend unification. |

## Steps

### Step 1: Identify Feature Coordination Points

Map Modbus scan UI, analysis result display, and preferences interactions currently coordinated by `NativeMainWindow`.

### Step 2: Create Narrow Controller Helper

Create a helper for feature decisions and state updates, not for HWND ownership.

### Step 3: Route State Decisions Through Helper

Move safe decision logic from `main_window_modbus.cpp`, `main_window_analysis.cpp`, and preferences handling.

### Step 4: Preserve User-Visible Behavior

Keep scan start/stop, analysis display, and preferences persistence behavior unchanged.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "native_modbus_scan_ui_state|native_analysis_report|native_ui_preferences"
```

## Verification

- [ ] UI behavior remains stable.
- [ ] Modbus/analysis/preferences state transitions are easier to test.
- [ ] No backend Modbus executor changes are made in this phase task.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "native_modbus_scan_ui_state|native_analysis_report|native_ui_preferences"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
refactor: extract native modbus analysis preference boundaries (Phase 1, Task 07)
```
