# Task 02: Migrate Command Write Capability

> Phase: 1 — Contract Foundation
> Status: Planned

---

## Objective

Replace the queue-owned `SerialWritePort` dependency in command sequences with the narrow session write capability while preserving command admission semantics and evidence metadata.

## Files

**Create:**
- None

**Modify:**
- `src/command_sequence/command_sequence.h`
- `src/command_sequence/command_sequence.cpp`

**Test:**
- `tests/command_sequence_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| None | None | None | Uses the neutral capability declared in `src/transport/serial_session.h`; no external library or runtime dependency. |

## Steps

### Step 1: Replace the command context pointer

Change `CommandSequenceExecutionContext::serialWriteTransport` from `SerialWritePort*` to the narrow session write-scheduler capability pointer.

### Step 2: Preserve the serial command input contract

Keep payload validation, the existing timeout field, safety limits, and the default timeout behavior unchanged while routing admission through the new capability.

### Step 3: Update the write-step call

Call the capability's enqueue operation from `runSerialWrite` and retain the missing-backend failure path.

### Step 4: Record structured metadata

Record request ID, generation, typed admission status/category, byte count, and deadline information instead of treating a localized message as a transport decision.

### Step 5: Preserve accepted-step semantics

Keep an accepted queue admission mapped to `CommandExecutionStatus::Completed`, because this command-sequence foundation does not wait for physical write completion.

### Step 6: Preserve assertion state

Continue storing the last write admission result so `LastSerialWriteAccepted` assertions and subsequent evidence events retain their current behavior.

### Step 7: Migrate command-sequence test doubles

Replace each direct `SerialWriteQueue` assignment in the command-sequence tests with a fake narrow scheduler that returns deterministic typed admission results.

### Step 8: Check the old write-port boundary is gone here

Run a focused source search and test suite to confirm command-sequence headers, implementation, and tests no longer include or name `SerialWritePort`.

## Verification

- [ ] Command sequence compilation depends only on the narrow write capability.
- [ ] Accepted, missing-backend, unsafe, and assertion paths retain their existing statuses.
- [ ] Evidence metadata contains request identity, generation, typed status/category, and byte count.
- [ ] No `SerialWritePort` reference remains in the three task files.

**Test command:**
```bash
cmake -S . -B build-phase1-command -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build build-phase1-command --target command_sequence_tests --parallel 2
ctest --test-dir build-phase1-command -R command_sequence_tests --output-on-failure
! rg -n "SerialWritePort" src/command_sequence tests/command_sequence_tests.cpp
```

**Expected output:**
```text
command_sequence_tests passes; the old SerialWritePort symbol is absent from the migrated files.
```

## Commit

```text
refactor: route command sequences through session write capability (Phase 1, Task 02)
```
