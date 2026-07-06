# Task 07: Add File Commit and Orphan Recovery

> Phase: 2 — Backend Consistency
> Status: pending

---

## Objective

Add schema-aware file commit and orphan recovery behavior for native storage records.

## Files

**Create:**
- None

**Modify:**
- `src/native_storage/native_store_file_ops.h`
- `src/native_storage/native_store_file_ops.cpp`
- `src/native_storage/native_store_record_io.h`
- `src/native_storage/native_store_record_io.cpp`
- `src/native_storage/native_store_record_codec.h`
- `src/native_storage/native_store_record_codec.cpp`
- `tests/native_storage_tests.cpp`
- `tests/session_store_tests.cpp`

**Test:**
- Native storage fault/recovery tests

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Internal file storage recovery only. |

## Steps

### Step 1: Define Commit Marker Discipline

Choose a minimal file-compatible marker or equivalent recovery signal for completed writes.

### Step 2: Add Recovery Scan

Detect and isolate incomplete/orphan records on startup or store open.

### Step 3: Preserve Existing Records

Ensure older records without new marker remain readable or are migrated safely.

### Step 4: Add Fault Injection Tests

Test partial write, truncated record, missing commit, duplicate commit, and recovery result reporting.

### Step 5: Run Storage Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "native_storage|session_store"
```

## Verification

- [ ] Partial writes do not corrupt committed session records.
- [ ] Recovery reports orphan handling.
- [ ] Existing storage fixtures remain compatible.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "native_storage|session_store"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
feat: add native storage commit recovery handling (Phase 2, Task 07)
```
