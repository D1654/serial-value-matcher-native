# Task 06: Harden Package Docs Dependency Gates

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

---

## Objective

Strengthen package audit, docs consistency, forbidden runtime, and release evidence checks for the native artifact.

## Files

**Create:**
- None

**Modify:**
- `scripts/inspect-windows-package.py`
- `scripts/inspect-windows-package.ps1`
- `scripts/check-docs-artifact-consistency.py`
- `.github/workflows/windows-native-package.yml`
- `docs/测试与验证.md`
- `docs/发布产物.md`

**Test:**
- Package audit scripts
- Docs consistency script

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| GitHub Actions artifact flow | actions/upload-artifact | artifact upload, missing-file behavior | Preserve package evidence and fail on missing artifacts. |

## Steps

### Step 1: Audit Current Package Gate

Review package summary terms, forbidden runtime list, hash generation, and docs links.

### Step 2: Add Missing Package Checks

Add checks for forbidden Qt/SQLite/.NET runtime files, unexpected DLLs, doc directory mismatch, and summary drift.

### Step 3: Harden Workflow Failure Modes

Ensure missing zip/hash/summary/self-test/UI perf/serial matrix files fail the workflow.

### Step 4: Update Docs Consistency

Add required terms for new version/evidence artifacts.

### Step 5: Run Scripts

```bash
python3 scripts/check-docs-artifact-consistency.py
```

## Verification

- [ ] Package audit fails on missing or forbidden runtime artifacts.
- [ ] Docs consistency catches release/doc drift.
- [ ] Workflow artifact list is explicit.

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
ci: harden native package docs dependency gates (Phase 3, Task 06)
```
