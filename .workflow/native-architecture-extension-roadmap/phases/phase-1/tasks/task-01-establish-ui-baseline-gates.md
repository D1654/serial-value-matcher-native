# Task 01: Establish UI Baseline Gates

> Phase: 1 — UI / Architecture Foundation
> Status: pending

---

## Objective

Make current native UI, package, docs, and performance validation assets explicit before any UI architecture changes.

## Files

**Create:**
- None

**Modify:**
- `docs/测试与验证.md`
- `docs/Windows原生UI验证.md`
- `.github/workflows/windows-native-ui-capture.yml`
- `scripts/capture-windows-native-ui-wine.sh`

**Test:**
- Existing CTest suite
- Existing native UI capture workflow/scripts

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| GitHub Actions artifact flow | actions/upload-artifact | artifact upload inputs, retention, if-no-files-found | Preserve UI screenshots and perf logs. |
| Win32 API | microsoft/Windows-classic-samples | window messages, DPI/layout reference patterns | Interpret UI capture and resize evidence. |

## Steps

### Step 1: Inventory Current UI Evidence

Review existing UI capture workflow, Wine capture script, and native `--ui-perf-test` output format.

```bash
sed -n '1,220p' .github/workflows/windows-native-ui-capture.yml
```

### Step 2: Document Baseline Semantics

Update UI validation docs to distinguish baseline evidence, blocking CI gates, and manual/local checks.

### Step 3: Record Baseline-Derived Performance Policy

Document that Phase 4 thresholds must be derived from current release/artifact output rather than invented abstract numbers.

### Step 4: Verify Artifact Names and Evidence Files

Check workflow output names still match docs and package consistency script expectations.

### Step 5: Run Docs Consistency

Run the existing docs/artifact consistency checker.

```bash
python3 scripts/check-docs-artifact-consistency.py
```

## Verification

- [ ] UI validation docs describe screenshot, resize, tab visibility, and `--ui-perf-test` baseline policy.
- [ ] PTY/local-only wording is not confused with UI capture gates.
- [ ] Docs consistency check passes.

**Test command:**
```bash
python3 scripts/check-docs-artifact-consistency.py
```

**Expected output:**
```
No docs/artifact consistency failures.
```

## Commit

```
docs: establish native UI baseline gate policy (Phase 1, Task 01)
```
