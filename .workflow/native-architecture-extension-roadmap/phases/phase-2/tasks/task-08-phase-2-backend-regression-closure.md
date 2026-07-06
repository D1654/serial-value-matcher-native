# Task 08: Phase 2 Backend Regression Closure

> Phase: 2 — Backend Consistency
> Status: pending

---

## Objective

Close Phase 2 by proving serial queue, Modbus executor, storage recovery, and backend stress gates are stable.

## Files

**Create:**
- None

**Modify:**
- `tests/quality_regression_tests.cpp`
- `tests/quality_stress_tests.cpp`
- `.github/workflows/windows-native-package.yml`
- `docs/测试与验证.md`

**Test:**
- Full CTest
- Native package workflow
- Local PTY loopback where supported

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| CMake | Kitware/CMake | CTest | Run and organize backend regression tests. |
| GitHub Actions artifact flow | actions/upload-artifact | artifact upload, retention | Preserve backend/package evidence. |

## Steps

### Step 1: Add Backend Regression Coverage

Update quality regression/stress tests for serial queue, Modbus executor, and storage recovery.

### Step 2: Keep Stress Tests Appropriately Gated

Ensure long-running tests remain opt-in unless explicitly suitable for CI.

### Step 3: Update Native Package Evidence

Ensure workflow preserves logs that prove backend closure.

### Step 4: Run Full Local Tests

```bash
ctest --test-dir build-codex --output-on-failure
```

### Step 5: Run Local PTY Evidence If Supported

```bash
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress python3 scripts/run-windows-native-serial-pty-loopback.py
```

## Verification

- [ ] Full CTest passes.
- [ ] Backend stress evidence exists.
- [ ] PTY loopback is either run locally or documented as environment-blocked.
- [ ] Native package workflow remains green.

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
test: close phase 2 backend regression gates (Phase 2, Task 08)
```
