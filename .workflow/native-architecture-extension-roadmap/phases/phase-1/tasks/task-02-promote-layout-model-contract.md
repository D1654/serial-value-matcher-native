# Task 02: Promote Layout Model Contract

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Ensure production resize and tab layout behavior is driven by `NativeLayoutModel` with edge-size tests.

## Files

**Create:**
- None

**Modify:**
- `src/win32/native_layout_model.h`
- `src/win32/native_layout_model.cpp`
- `src/win32/main_window_layout.cpp`
- `tests/native_ui_layout_model_tests.cpp`

**Test:**
- `tests/native_ui_layout_model_tests.cpp`
- `tests/native_layout_metrics_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | `WM_SIZE`, DPI/layout references | Validate production layout behavior around resize and DPI. |

## Steps

### Step 1: Map Current Production Layout Inputs

Identify all geometry and visibility decisions currently computed outside `NativeLayoutModel`.

### Step 2: Extend Layout Model Outputs

Add missing model fields for tab area, log area, prompt area, split constraints, and key control bounds.

### Step 3: Route Production Layout Through Model

Update `main_window_layout.cpp` so manual geometry decisions consume the model instead of duplicating calculations.

### Step 4: Add Edge-Size Tests

Add tests for minimum window, narrow width, tall/short split, all tabs, and current-page prompt visibility.

### Step 5: Run Focused Tests

Run layout model and layout metrics tests.

```bash
ctest --test-dir build-codex --output-on-failure -R "native_ui_layout_model|native_layout_metrics"
```

## Verification

- [ ] Production layout path consumes `NativeLayoutModel` for key regions.
- [ ] Edge-size tests cover known tab blank/control invisibility risks.
- [ ] Focused CTest subset passes.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "native_ui_layout_model|native_layout_metrics"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
refactor: promote native layout model contract (Phase 1, Task 02)
```
