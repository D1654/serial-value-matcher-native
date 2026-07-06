# Task 05: Single-Source Version Metadata

> Phase: 3 — Extension Capability & Production Hardening
> Status: pending

---

## Objective

Align CMake version, Win32 VERSIONINFO, package name, README/docs, release notes, and artifact summary.

## Files

**Create:**
- `cmake/svm_version.cmake`
- `tests/version_metadata_tests.cpp`

**Modify:**
- `CMakeLists.txt`
- `src/win32/app.rc`
- `scripts/inspect-windows-package.py`
- `README.md`
- `docs/发布产物.md`
- `docs/Windows发布说明.md`

**Test:**
- `tests/version_metadata_tests.cpp`
- Package audit scripts

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| CMake | Kitware/CMake | project version, configure variables, resource generation | Single-source version metadata. |
| Win32 API | microsoft/Windows-classic-samples | `VERSIONINFO` resource pattern | Native executable file/product metadata. |

## Steps

### Step 1: Define Version Source

Create one CMake-level version metadata source consumed by build/package scripts.

### Step 2: Wire Win32 Resource Metadata

Ensure `app.rc` receives file/product version and display strings consistently.

### Step 3: Update Package Audit

Make package audit report version metadata and fail on drift where practical.

### Step 4: Update Docs

Ensure README and release docs refer to artifact/version names consistently.

### Step 5: Add Tests

Add test/script checks for metadata consistency.

## Verification

- [ ] CMake, VERSIONINFO, package summary, README, and docs agree.
- [ ] Package audit reports version metadata.
- [ ] Tests/docs consistency pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "version_metadata"
python3 scripts/check-docs-artifact-consistency.py
```

**Expected output:**
```
100% tests passed
No docs/artifact consistency failures.
```

## Commit

```
build: single-source native version metadata (Phase 3, Task 05)
```
