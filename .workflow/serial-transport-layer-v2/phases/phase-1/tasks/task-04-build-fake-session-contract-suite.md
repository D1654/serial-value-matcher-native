# Task 04: Build Fake Session Contract Suite

> Phase: 1 — Contract Foundation
> Status: Completed

---

## Objective

Replace the broad fake transport with a deterministic fake session suite that proves state, generation, typed-result, deadline, cancellation, close, and stale-completion behavior.

## Files

**Create:**
- None

**Modify:**
- `tests/transport_contract_tests.cpp`
- `CMakeLists.txt`

**Test:**
- `tests/transport_contract_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| None | None | None | Depends only on the Phase 1 neutral contracts and deterministic C++20 test doubles. |

Internal prerequisites: Phase 1 Task 01 session model, Task 02 write capability, and Task 03 queue accounting.

## Steps

### Step 1: Replace the broad fake type

Refactor `FakeSerialTransport` into a fake session that implements the session, byte, and write-scheduler capabilities declared in `serial_session.h`.

### Step 2: Test lifecycle transitions

Assert valid `closed -> opening -> open -> closing -> closed` transitions and reject operations that are submitted while the session is closed, opening, closing, or faulted.

### Step 3: Test generation publication

Open successive sessions and assert monotonic generation values, endpoint snapshots, and no publication of a replacement generation before the old one is invalidated.

### Step 4: Test typed byte results

Return deterministic successful, short-write, read-error, closed, and native-error results and assert status, category, byte count, native code, endpoint, and generation fields.

### Step 5: Test queue admission and snapshots

Exercise count and byte backpressure, active-inclusive accounting, FIFO request IDs, deadlines, and snapshot watermarks through the fake scheduler.

### Step 6: Test cancellation and close settlement

Cancel pending and active work, close the session during work, and assert that every accepted request produces exactly one terminal result within the approved fake deadline model.

### Step 7: Test stale-result suppression

Deliver a completion tagged with an older generation after reopen and assert that the fake session rejects it without changing current state, queue accounting, or the new session's evidence.

### Step 8: Register the focused suite

Rename the CMake test target and CTest registration from the broad transport-contract name to `serial_session_contract_tests`, then build and run only this suite.

## Verification

- [x] Fake lifecycle and generation assertions cover every approved state and reconnect rule.
- [x] Typed results carry native evidence without requiring localized message parsing.
- [x] Close, cancel, and stale-generation cases prove exactly-once terminal settlement.
- [x] The test target links only neutral core code and has no Win32/UI include dependency.

**Test command:**
```bash
cmake -S . -B build-phase1-session-tests -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build build-phase1-session-tests --target serial_session_contract_tests --parallel 2
ctest --test-dir build-phase1-session-tests -R serial_session_contract_tests --output-on-failure
! rg -n '#include <windows.h>|#include "win32/' src/transport/serial_session.h
```

**Expected output:**
```text
serial_session_contract_tests passes; 100% of the selected tests passed; the neutral session header search has no matches.
```

## Commit

```text
test: add deterministic serial session contract suite (Phase 1, Task 04)
```
