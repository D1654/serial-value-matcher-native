# Task 02: Enforce Transport Boundaries

> Phase: 4 — Boundary and Release Closure
> Status: Completed

---

## Objective

Add a deterministic repository check that prevents neutral transport code from depending on UI, matching/codec, Gray-code, old-facade, or concrete Win32 implementation details.

## Files

**Create:**

- `scripts/check-transport-boundaries.py`

**Modify:**

- `CMakeLists.txt`
- `scripts/check-docs-artifact-consistency.py`
- `docs/架构说明.md`

**Test:**

- `scripts/check-transport-boundaries.py`
- CTest target registered in `CMakeLists.txt`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| CMake/CTest | `Kitware/CMake` | `add_test`, `ctest --test-dir`, test properties | Register and execute the architecture boundary check on every host build. |

## Steps

### Step 1: Define Explicit Boundary Rules

List the neutral transport files and forbidden dependency directions. At minimum reject includes/references to `src/win32`, UI/main-window types, matching/codec types, Gray-code implementation symbols, `SerialTransport`, and `Win32SerialPort` from neutral transport contracts.

### Step 2: Create the Boundary Checker

Create `scripts/check-transport-boundaries.py` using only the Python standard library. Resolve the repository root deterministically, scan the declared files, and report each violation with file, line, matched rule, and remediation hint.

### Step 3: Add Script Self-Checks

Add a `--self-test` mode with in-memory allowed and forbidden samples so rule changes can be validated without modifying repository fixtures.

### Step 4: Register the CTest Gate

Add a CTest entry in `CMakeLists.txt` that runs the checker from the repository root and fails on any violation.

### Step 5: Extend Documentation Consistency

Update `scripts/check-docs-artifact-consistency.py` to require the new script, CTest registration, architecture terms, and the explicit Gray-code-not-implemented statement.

### Step 6: Update the Architecture Index

Update `docs/架构说明.md` to point to the session contract, Win32 owner, RTU adapter, future codec boundary, and automated boundary gate.

### Step 7: Run Direct Checks

Run the checker normally and in self-test mode; fix rule precision until both pass on the current source tree.

### Step 8: Run Integrated Checks

Configure a fresh host build, execute the boundary CTest and documentation consistency check, then run the full host CTest suite to ensure registration does not disturb unrelated tests.

## Verification

- [x] Direct and self-test modes exit 0 on the valid repository.
- [x] The self-test proves each forbidden dependency category is rejected.
- [x] CTest lists and passes the boundary test.
- [x] Documentation consistency and the full host CTest suite pass.

**Test command:**

```bash
python3 scripts/check-transport-boundaries.py --self-test
python3 scripts/check-transport-boundaries.py
cmake -S . -B /tmp/svm-transport-v2-boundary -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-boundary --parallel
ctest --test-dir /tmp/svm-transport-v2-boundary -R 'transport.*boundar' --output-on-failure
ctest --test-dir /tmp/svm-transport-v2-boundary --output-on-failure
python3 scripts/check-docs-artifact-consistency.py
```

**Expected output:**

```text
Boundary self-test: passed.
Transport boundary check: passed.
Focused and full CTest runs report 100% tests passed.
Documentation consistency check exits 0.
```

## Commit

```text
test: enforce serial transport boundaries (Phase 4, Task 02)
```
