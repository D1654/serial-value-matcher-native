# Task 05: Define Main Window Shell Seams

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Make `NativeMainWindow` shell responsibilities explicit and create narrow seams for feature coordination without broad file movement.

## Files

**Create:**
- `src/win32/native_main_window_context.h`

**Modify:**
- `src/win32/main_window.h`
- `src/win32/main_window_messages.cpp`
- `src/win32/main_window_commands.cpp`
- `src/win32/main_window_lifecycle.cpp`

**Test:**
- Existing native self-test
- Existing CTest suite

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | `WndProc`, message dispatch, lifecycle patterns | Keep shell responsibilities separate from feature behavior. |

## Steps

### Step 1: Classify Main Window Responsibilities

Separate message dispatch, lifetime, HWND ownership, command routing, and feature behavior responsibilities.

### Step 2: Create Context Header

Add a narrow `native_main_window_context.h` only for shared shell context that multiple feature helpers need.

### Step 3: Reduce Direct Cross-Feature Calls

Update message and command handlers to route through clear helper boundaries where already possible.

### Step 4: Preserve HWND Ownership

Keep HWND creation and lifetime in `NativeMainWindow`; do not move HWND ownership into controllers.

### Step 5: Run Native Self-Test

```bash
ctest --test-dir build-codex --output-on-failure
```

## Verification

- [ ] `NativeMainWindow` shell responsibilities are explicit.
- [ ] No broad directory move or plugin framework is introduced.
- [ ] Full local CTest passes.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure
```

**Expected output:**
```
100% tests passed
```

## Commit

```
refactor: define native main window shell seams (Phase 1, Task 05)
```
