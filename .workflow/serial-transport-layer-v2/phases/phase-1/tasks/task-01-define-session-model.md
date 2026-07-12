# Task 01: Define Session Model

> Phase: 1 — Contract Foundation
> Status: Planned

---

## Objective

Define the neutral session state, operation identity, typed results, and narrow capability contracts without exposing Win32 or UI types.

## Files

**Create:**
- `src/transport/serial_session.h`

**Modify:**
- `src/transport/serial_types.h`

**Test:**
- `tests/transport_contract_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| None | None | None | C++20 standard-library value types only; no external dependency or runtime API is added. |

## Steps

### Step 1: Record the existing transport obligations

List the lifecycle, byte I/O, line-control, queued-write, and snapshot operations that must remain available so the new contracts preserve current behavior.

### Step 2: Declare the session state model

Add neutral `closed`, `opening`, `open`, `closing`, and `faulted` states with a stable name helper for tests and diagnostics.

### Step 3: Define operation identity types

Add non-zero request/operation identifiers and a session-generation value type with documented zero/unassigned behavior.

### Step 4: Define deadline and operation descriptors

Represent an absolute or monotonic deadline and operation kind without storing localized text or including Win32 headers.

### Step 5: Define typed error evidence

Add stable error categories for invalid input, closed session, rejected queue work, timeout, cancellation, disconnect, native failure, and protocol-independent I/O failure, plus an optional native error code and byte count.

### Step 6: Define structured operation results

Add read, write-admission, and terminal result values carrying request ID, generation, endpoint, deadline outcome, status, transferred bytes, and native evidence.

### Step 7: Declare the session capability

Declare the lifecycle/session capability for open, close, state, generation, endpoint, configuration snapshot, and control-line operations; require close/reopen semantics to settle or invalidate old work.

### Step 8: Declare byte and write-scheduler capabilities

Declare the byte-stream and queued-write capabilities in `serial_session.h`, keeping them non-owning views implemented by the one session owner and avoiding a replacement broad facade.

### Step 9: Update the neutral type test seam

Update the existing transport contract test's compile-time fake and assertions to use the new neutral types while leaving full state, generation, and queue race coverage to Task 4.

## Verification

- [ ] The new session header contains no Win32, Qt, UI, or storage include.
- [ ] Every structured result carries request identity and session generation where an operation can outlive its call site.
- [ ] Capability declarations do not own a native handle or duplicate the old `SerialTransport` facade.
- [ ] The focused fake contract test still builds and passes after the type migration.

**Test command:**
```bash
cmake -S . -B build-phase1-contract -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build build-phase1-contract --target transport_contract_tests --parallel 2
ctest --test-dir build-phase1-contract -R transport_contract_tests --output-on-failure
```

**Expected output:**
```text
transport_contract_tests passes; 100% of the selected tests passed.
```

## Commit

```text
feat: define neutral serial session model (Phase 1, Task 01)
```
