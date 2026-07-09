# Task 02: Add Evidence Export and Redaction

> Phase: 3 — Extension Capability & Production Hardening
> Status: completed

---

## Objective

Export local diagnostic/evidence bundles with optional redaction and no upload behavior.

## Files

**Create:**
- `src/report/evidence_bundle_writer.h`
- `src/report/evidence_bundle_writer.cpp`
- `tests/evidence_bundle_writer_tests.cpp`

**Modify:**
- `src/native_storage/native_session_store.h`
- `src/native_storage/native_session_store.cpp`
- `src/win32/main_window.h`
- `src/win32/main_window_analysis.cpp`
- `src/win32/main_window_commands.cpp`
- `src/win32/main_window_menu.cpp`
- `src/win32/native_analysis_workflow.h`
- `src/win32/native_analysis_workflow.cpp`
- `src/win32/native_main_window_context.h`
- `src/win32/resource.h`
- `src/win32/ui_text.h`
- `src/win32/ui_text.cpp`
- `docs/故障排查.md`
- `docs/用户指南.md`

**Test:**
- `tests/evidence_bundle_writer_tests.cpp`
- `tests/native_storage_tests.cpp`
- `tests/native_win32_analysis_workflow_tests.cpp`

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

- [x] Evidence bundle export is local-only.
- [x] Redaction removes configured sensitive fields.
- [x] Tests and docs consistency pass.

Additional verification:

- `ctest --test-dir build-codex --output-on-failure -R "evidence_bundle_writer|rule_verification_report|native_storage_tests"`: 3/3 passed.
- `ctest --test-dir build-windows-native-mingw --output-on-failure -R "evidence_bundle_writer|native_storage_tests|native_win32_analysis_workflow_tests"`: 3/3 passed.
- `ctest --test-dir build-codex --output-on-failure`: 52/52 passed.
- `ctest --test-dir build-windows-native-mingw --output-on-failure`: 29/29 passed.
- `python3 scripts/check-docs-artifact-consistency.py`: passed.
- `bash scripts/package-windows-native-mingw.sh`: passed; zip 875150 bytes, extracted 2185500 bytes, Wine gate passed.

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
