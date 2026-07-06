# Task 08: Phase 1 UI Regression Closure

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Close Phase 1 by proving key tabs, resize paths, UI perf, self-test, docs consistency, and Actions UI evidence remain valid.

## Files

**Create:**
- None

**Modify:**
- `.github/workflows/windows-native-ui-capture.yml`
- `scripts/capture-windows-native-ui.ps1`
- `scripts/capture-windows-native-ui-wine.sh`
- `docs/Windows原生UI验证.md`

**Test:**
- Existing native UI capture scripts
- Existing CTest suite

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| GitHub Actions artifact flow | actions/upload-artifact | artifact upload, retention, path collection | Preserve UI screenshots, window info, and perf logs. |
| CMake | Kitware/CMake | CTest | Run complete local/CI verification. |

## Steps

### Step 1: Expand UI Evidence Checklist

Ensure UI capture documents all critical tabs and resize states.

### Step 2: Run Local CTest

```bash
ctest --test-dir build-codex --output-on-failure
```

### Step 3: Run Native Self-Test and UI Perf

Run available native self-test/UI perf commands for the current build artifact.

### Step 4: Run UI Capture

Use Windows or Wine capture script depending on environment.

### Step 5: Update Documentation

Record the expected artifacts and review checklist in the UI verification doc.

## Verification

- [ ] All tests pass.
- [ ] UI capture artifacts exist and cover key views.
- [ ] UI perf does not regress relative to baseline-derived thresholds.
- [ ] Docs consistency passes.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure
python3 scripts/check-docs-artifact-consistency.py
```

**Expected output:**
```
100% tests passed
No docs/artifact consistency failures.
```

## Commit

```
test: close phase 1 native ui regression gates (Phase 1, Task 08)
```
