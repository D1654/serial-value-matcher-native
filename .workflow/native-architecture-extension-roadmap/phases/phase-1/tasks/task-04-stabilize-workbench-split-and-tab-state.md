# Task 04: Stabilize Workbench Split and Tab State

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Protect tab/log split behavior, current-page prompt layout, and resizable panel state from hidden-control or flicker regressions.

## Files

**Create:**
- None

**Modify:**
- `src/win32/native_workbench_tab_state.h`
- `src/win32/native_workbench_tab_state.cpp`
- `src/win32/native_log_scroll_state.h`
- `src/win32/native_log_scroll_state.cpp`
- `src/win32/main_window_layout.cpp`
- `tests/native_workbench_tab_state_tests.cpp`
- `tests/native_log_scroll_state_tests.cpp`

**Test:**
- `tests/native_workbench_tab_state_tests.cpp`
- `tests/native_log_scroll_state_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | child-window layout, message loop references | Validate tab/split layout behavior in a native window. |

## Steps

### Step 1: Define Split Constraints

Document and encode minimum/maximum heights for log area, tab area, prompt visibility, and control padding.

### Step 2: Preserve Current-Page Prompt Visibility

Ensure prompt placement remains readable while dragging split height.

### Step 3: Normalize Tab State Transitions

Ensure switching tabs never depends on hover/focus to make controls visible.

### Step 4: Add Regression Tests

Cover initial single-send tab, multi-send tab after switching, file tab controls, and prompt area constraints.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "native_workbench_tab_state|native_log_scroll_state"
```

## Verification

- [ ] Tab controls are visible without hover/focus workarounds.
- [ ] Split resizing preserves prompt readability.
- [ ] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "native_workbench_tab_state|native_log_scroll_state"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
fix: stabilize native workbench split tab state (Phase 1, Task 04)
```
