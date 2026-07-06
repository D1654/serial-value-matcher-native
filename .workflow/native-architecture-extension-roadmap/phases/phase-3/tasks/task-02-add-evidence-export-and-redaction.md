# Task 02: Add Evidence Export and Redaction

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

---

## Objective

Export local diagnostic/evidence bundles with optional redaction and no upload behavior.

## Files

**Create:**
- `src/report/evidence_bundle_writer.h`
- `src/report/evidence_bundle_writer.cpp`
- `tests/evidence_bundle_writer_tests.cpp`

**Modify:**
- `src/report/rule_verification_report.h`
- `src/report/rule_verification_report.cpp`
- `src/native_storage/native_session_store.h`
- `src/native_storage/native_session_store.cpp`
- `docs/故障排查.md`
- `docs/用户指南.md`

**Test:**
- `tests/evidence_bundle_writer_tests.cpp`
- `tests/rule_verification_report_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Local file export only. |

## Steps

### Step 1: Define Bundle Contents

List required and optional evidence files, including raw events, summary, app version, scan settings, and report metadata.

### Step 2: Add Redaction Options

Support redacting absolute paths, device identifiers, and raw business payload where requested.

### Step 3: Write Local Bundle Only

Export to user-selected local path; do not add upload, telemetry, account, or cloud behavior.

### Step 4: Add Tests

Verify full bundle, redacted bundle, missing optional fields, and path privacy behavior.

### Step 5: Update Documentation

Document complete-vs-redacted export and user responsibility for sharing files.

## Verification

- [ ] Evidence bundle export is local-only.
- [ ] Redaction removes configured sensitive fields.
- [ ] Tests and docs consistency pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "evidence_bundle_writer|rule_verification_report"
python3 scripts/check-docs-artifact-consistency.py
```

**Expected output:**
```
100% tests passed
No docs/artifact consistency failures.
```

## Commit

```
feat: add local evidence bundle redaction export (Phase 3, Task 02)
```
