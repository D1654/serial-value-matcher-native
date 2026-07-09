# Task 05: Single-Source Version Metadata

> Phase: 3 — Extension Capability & Production Hardening
> Status: completed

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

- [x] CMake, VERSIONINFO, package summary, README, and docs agree.
- [x] Package audit reports version metadata.
- [x] Tests/docs consistency pass.

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

## Completion Notes

- Added `cmake/svm_version.cmake` as the single editable source for version, release tag, product strings, executable name, and package artifact names.
- Generated `svm_version_resource.h` from CMake and wired Win32 `VERSIONINFO` plus evidence-bundle app version to the generated metadata.
- Extended Python and PowerShell package inspectors to report and gate VERSIONINFO drift.
- Added `version_metadata_tests` and updated README/release docs/package docs to describe version metadata fields.
- Verification completed: focused version metadata tests on build-codex and MinGW, docs consistency, full build-codex 55/55, full MinGW 32/32, local MinGW package/Wine gate with VERSIONINFO summary.

## Commit

```
build: single-source native version metadata (Phase 3, Task 05)
```
