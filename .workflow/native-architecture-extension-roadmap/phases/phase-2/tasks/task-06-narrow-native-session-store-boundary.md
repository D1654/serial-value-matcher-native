# Task 06: Narrow Native Session Store Boundary

> Phase: 2 — Backend Consistency
> Status: completed

---

## Objective

Separate storage interface expectations from native file backend behavior without introducing SQLite.

## Files

**Create:**
- `src/storage/session_store_port.h`

**Modify:**
- `src/native_storage/native_session_store.h`
- `src/native_storage/native_store_files.h`
- `src/native_storage/native_store_files.cpp`
- `src/storage/session_store.h`
- `tests/native_storage_tests.cpp`
- `tests/session_store_tests.cpp`

**Test:**
- Native storage and session store tests

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Internal storage boundary only. |

## Steps

### Step 1: Inventory Store Responsibilities

Identify append, query, cache, schema, recovery, and report/evidence responsibilities.

### Step 2: Define Narrow Port

Create a storage port interface that expresses current required operations without SQLite assumptions.

### Step 3: Adapt Native Store

Make `NativeSessionStore` implement or align with the narrow port while preserving existing file format.

### Step 4: Preserve Compatibility

Keep existing session data readable and writable.

### Step 5: Add Boundary Tests

Test current store operations through the narrow boundary.

## Verification

- [x] Native file backend remains compatible.
- [x] Storage interface is narrower than the facade.
- [x] Tests pass without adding SQLite.

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
refactor: narrow native session store boundary (Phase 2, Task 06)
```
