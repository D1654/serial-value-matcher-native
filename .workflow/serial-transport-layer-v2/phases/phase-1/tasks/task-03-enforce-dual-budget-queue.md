# Task 03: Enforce Dual-Budget Queue

> Phase: 1 — Contract Foundation
> Status: Completed

---

## Objective

Make queued serial writes bounded by 64 counted requests and 256 KiB of counted work, including active work, with deterministic deadlines and release semantics.

## Files

**Create:**
- None

**Modify:**
- `src/transport/serial_write_queue.h`
- `src/transport/serial_write_queue.cpp`

**Test:**
- `tests/serial_write_queue_tests.cpp`
- `tests/native_file_send_state_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| None | None | None | Uses C++20 containers and clocks already available in the project; no external dependency. |

## Steps

### Step 1: Define the dual-budget defaults

Set the neutral queue defaults to 64 requests and 262144 counted bytes while retaining a named timeout default for existing callers.

### Step 2: Extend queue configuration

Add request-count and byte-capacity configuration values and reject zero or otherwise invalid limits deterministically.

### Step 3: Extend request accounting

Track payload byte size, generation, and the explicit request deadline needed to settle work without relying on the port-wide timeout.

### Step 4: Extend queue snapshots

Expose pending count, active count, pending bytes, active bytes, configured limits, and next request ID in one neutral snapshot.

### Step 5: Implement admission checks

Reject empty/invalid requests, count overflow, or byte overflow immediately without blocking, reordering, dropping, or mutating older queued work.

### Step 6: Define active-work reservation

Make the transition from pending to active preserve count and byte accounting until a terminal result releases the reservation.

### Step 7: Define cancellation release

Return one typed terminal cancellation result for each cancelled pending request and release its count and bytes exactly once.

### Step 8: Define completion release

Release active reservations on sent, failed, timed-out, disconnected, or closed outcomes and retain transferred byte counts for partial writes.

### Step 9: Preserve FIFO and ID guarantees

Keep FIFO order for admission and activation, keep request IDs monotonic with wrap protection, and ensure terminal completion cannot be emitted twice.

### Step 10: Extend focused queue and file-send tests

Cover count-full, byte-full, active-inclusive limits, byte release after every terminal path, deadlines, cancellation, FIFO, and the existing file-send acceptance/completion behavior.

## Verification

- [x] A 65th counted request is rejected when the default count budget is occupied.
- [x] A payload that would exceed 256 KiB is rejected immediately without removing existing work.
- [x] Pending plus active count/bytes never exceeds configured limits.
- [x] Every terminal path releases its reservation once and preserves FIFO/monotonic IDs.
- [x] File-send queue behavior remains accepted/sent for valid chunks.

**Test command:**
```bash
cmake -S . -B build-phase1-queue -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build build-phase1-queue --target serial_write_queue_tests native_file_send_state_tests --parallel 2
ctest --test-dir build-phase1-queue -R "serial_write_queue_tests|native_file_send_state_tests" --output-on-failure
```

**Expected output:**
```text
serial_write_queue_tests and native_file_send_state_tests pass; 100% of the selected tests passed.
```

## Commit

```text
feat: enforce counted serial queue budgets (Phase 1, Task 03)
```
