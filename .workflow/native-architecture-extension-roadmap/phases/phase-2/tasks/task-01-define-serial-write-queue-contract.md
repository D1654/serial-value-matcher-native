# Task 01: Define Serial Write Queue Contract

> Phase: 2 — Backend Consistency
> Status: pending

---

## Objective

Define a bounded serial write request/result contract that can support manual send, batch send, and future command sequences without blocking UI.

## Files

**Create:**
- `src/transport/serial_write_queue.h`
- `src/transport/serial_write_queue.cpp`
- `tests/serial_write_queue_tests.cpp`

**Modify:**
- `src/transport/serial_port_service.h`
- `src/transport/serial_port_service.cpp`
- `src/win32/native_serial_io_state.h`
- `src/win32/native_serial_io_state.cpp`

**Test:**
- `tests/serial_write_queue_tests.cpp`
- `tests/native_serial_io_state_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Core queue contract uses standard C++ only. |

## Steps

### Step 1: Define Request and Result Types

Add request id, payload, timeout, cancellation token/state, and result enum for accepted/sent/failed/timeout/cancelled.

### Step 2: Define Bounded Queue Behavior

Specify maximum queue depth, backpressure result, FIFO semantics, and cancellation rules.

### Step 3: Integrate State Representation

Expose queue status through `native_serial_io_state` without adding HWND dependencies.

### Step 4: Add Unit Tests

Cover enqueue, full queue, cancel before send, timeout, failure, and successful completion.

### Step 5: Run Focused Tests

```bash
ctest --test-dir build-codex --output-on-failure -R "serial_write_queue|native_serial_io_state"
```

## Verification

- [ ] Queue contract is independent of Win32 HWNDs.
- [ ] Backpressure and result states are explicit.
- [ ] Focused tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "serial_write_queue|native_serial_io_state"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
feat: define bounded serial write queue contract (Phase 2, Task 01)
```
