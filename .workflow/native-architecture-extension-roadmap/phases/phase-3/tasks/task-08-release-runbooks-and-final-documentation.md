# Task 08: Release Runbooks and Final Documentation

> Phase: 3 — Extension Capability & Production Hardening
> Status: completed

---

## Objective

Update Chinese-first runbooks for build, artifact verification, UI review, serial stress, release, rollback, and diagnostics.

## Files

**Create:**
- None

**Modify:**
- `README.md`
- `docs/开发者指南.md`
- `docs/测试与验证.md`
- `docs/发布产物.md`
- `docs/Windows发布说明.md`
- `docs/故障排查.md`
- `docs/用户指南.md`

**Test:**
- Docs consistency script

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Documentation-only task. |

## Steps

### Step 1: Update User-Facing README

Ensure README matches current native artifact, screenshots, docs, and release workflow.

### Step 2: Update Developer Runbook

Document local build/test, CTest, package, UI capture, serial PTY evidence, and docs consistency.

### Step 3: Update Release Runbook

Document artifact download, package audit, hash, UI review, release creation/update, rollback, and re-release.

### Step 4: Update Troubleshooting

Document diagnostic bundle, redaction, crash/startup/session recovery, serial issues, and UI artifact review.

### Step 5: Run Docs Consistency

```bash
python3 scripts/check-docs-artifact-consistency.py
```

## Verification

- [x] README and docs are Chinese-first and match current release artifact names.
- [x] Runbooks cover build, test, package, UI, serial, release, rollback, and diagnostics.
- [x] Docs consistency passes.

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
docs: update native release runbooks documentation (Phase 3, Task 08)
```
