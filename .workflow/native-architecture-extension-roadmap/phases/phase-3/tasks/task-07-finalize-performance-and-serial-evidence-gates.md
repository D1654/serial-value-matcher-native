# Task 07: Finalize Performance and Serial Evidence Gates

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

---

## Objective

Turn baseline-derived UI performance and serial fake/local PTY evidence into explicit release-candidate gates.

## Files

**Create:**
- None

**Modify:**
- `scripts/capture-windows-native-ui.ps1`
- `scripts/capture-windows-native-ui-wine.sh`
- `scripts/run-windows-native-serial-pty-loopback.py`
- `.github/workflows/windows-native-ui-capture.yml`
- `.github/workflows/windows-native-package.yml`
- `docs/Windows原生UI验证.md`
- `docs/Windows串口真机验收.md`

**Test:**
- UI capture scripts
- Serial PTY script where environment supports it
- Docs consistency script

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| GitHub Actions artifact flow | actions/upload-artifact | artifact upload, retention, step summary | Preserve performance and serial evidence. |
| Win32 API | microsoft/Windows-classic-samples | UI message/resize references | Interpret UI perf and screenshot evidence. |

## Steps

### Step 1: Define UI Perf Baseline

Use current release/artifact `--ui-perf-test` output to establish baseline-derived acceptance language.

### Step 2: Add Evidence Summary

Ensure capture scripts write stable summaries for UI perf, resize, screenshots, and key tab checks.

### Step 3: Clarify Serial Evidence

Keep fake/native tests CI-blocking and PTY loopback local release-candidate evidence unless a CI substitute is implemented.

### Step 4: Update Workflows

Ensure workflow artifacts include all relevant logs and summaries.

### Step 5: Update Docs

Document exact commands and artifact review expectations.

## Verification

- [ ] UI perf gate is baseline-derived.
- [ ] Serial PTY local-only status is explicit when applicable.
- [ ] Docs consistency passes.

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
ci: finalize native performance serial evidence gates (Phase 3, Task 07)
```
