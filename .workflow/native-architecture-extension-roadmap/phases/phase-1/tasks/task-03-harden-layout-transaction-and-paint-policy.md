# Task 03: Harden Layout Transaction and Paint Policy

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Reduce resize flicker by batching HWND movement and aligning repaint/frame scheduling with the layout transaction path.

## Files

**Create:**
- None

**Modify:**
- `src/win32/native_layout_transaction.h`
- `src/win32/native_layout_transaction.cpp`
- `src/win32/native_paint_policy.h`
- `src/win32/native_paint_policy.cpp`
- `src/win32/native_frame_scheduler.h`
- `src/win32/native_frame_scheduler.cpp`
- `src/win32/main_window_messages.cpp`
- `tests/native_ui_layout_transaction_tests.cpp`
- `tests/native_paint_policy_tests.cpp`
- `tests/native_frame_scheduler_tests.cpp`

**Test:**
- `tests/native_ui_layout_transaction_tests.cpp`
- `tests/native_paint_policy_tests.cpp`
- `tests/native_frame_scheduler_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | `BeginPaint`, `EndPaint`, `WM_PAINT`, `WM_ERASEBKGND`, `MoveWindow` | Repaint minimization and batched child-window layout. |

## Steps

### Step 1: Audit Resize and Paint Message Flow

Trace `WM_SIZE`, paint invalidation, frame scheduling, and layout transaction commit order.

### Step 2: Make Transaction Boundaries Explicit

Ensure a resize operation has one layout transaction and a predictable commit/redraw order.

### Step 3: Apply Paint Policy During Resize

Use `NativePaintPolicy` to avoid unnecessary background erase and repeated invalidation.

### Step 4: Add Regression Tests

Test duplicate commit suppression, redraw policy choices, and frame scheduling behavior.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "native_ui_layout_transaction|native_paint_policy|native_frame_scheduler"
```

## Verification

- [ ] Layout transaction produces stable commit order.
- [ ] Paint policy explicitly handles resize-heavy paths.
- [ ] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "native_ui_layout_transaction|native_paint_policy|native_frame_scheduler"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
perf: harden native layout transaction repaint path (Phase 1, Task 03)
```
