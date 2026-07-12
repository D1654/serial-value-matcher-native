# Task 2: Implement Generation and Settlement

> Phase: 2 — Win32 Session and Caller Migration
> Status: pending

---

## Objective

Add explicit session states, generation-aware operation identity, deadlines, settlement of pending and active work, and stable native error categories to `Win32SerialSession` before any caller migration depends on them.

## Files

**Create:**

- None

**Modify:**

- `src/win32/win32_serial_session.h`
- `src/win32/win32_serial_session.cpp`
- `tests/native_win32_serial_tests.cpp`
- `tests/native_reconnect_state_tests.cpp`

**Delete:**

- None

**Test:**

- `tests/native_win32_serial_tests.cpp`
- `tests/native_reconnect_state_tests.cpp`

## Dependencies

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | `GetLastError`, `FormatMessageW`, `CancelIoEx`, `PurgeComm`, `WaitForSingleObject`, `SetEvent`, `ResetEvent`, `CloseHandle` | Capture native failure evidence, request cancellation, drain worker activity, and settle the old generation before close. |
| C++ standard library | None | `std::chrono`, `std::atomic`, `std::optional`, `std::variant`, `std::uint64_t` | Represent state, generation, deadlines, and typed operation outcomes without a runtime dependency. |

## Steps

### Step 1: Inventory the Phase 1 result model

Map the session state, operation kind, terminal status, generation, request ID, deadline, byte count, and native error fields from the Phase 1 contracts to the existing Win32 worker paths. Do not reintroduce localized text as a decision field.

### Step 2: Add explicit session state storage

Add `closed`, `opening`, `open`, `closing`, and `faulted` state tracking to `Win32SerialSession`. Make state transitions occur only inside the session's existing synchronization boundary.

### Step 3: Allocate and publish generations

Keep a monotonic generation counter. Increment it when a new open succeeds, publish it only after configuration and control lines are ready, and attach it to every accepted write, read observation, and terminal result.

### Step 4: Invalidate before close

Transition to `closing`, invalidate the published generation before stopping workers, and reject new work while closing. Ensure a replacement open cannot observe the old generation.

### Step 5: Settle pending work

Convert all queued requests rejected by close or reconnect into one terminal `Cancelled` or `Disconnected` result according to the Phase 1 contract. Preserve request ID, generation, deadline, and byte count in each result.

### Step 6: Settle active work

Mark the active request as cancelling, signal its wake event, request the supported native cancellation, wait for the worker to finish, and publish exactly one terminal result before releasing its buffer or the handle.

### Step 7: Enforce request deadlines

Compute each request deadline from its enqueue time and timeout value. Make the synchronous worker classify an expired request as `Timeout` rather than relying only on the port-wide `COMMTIMEOUTS` text.

### Step 8: Map native failures

Store the Win32 error code, operation kind, and stable transport category in the structured result. Keep human-readable formatting as diagnostic evidence only; do not branch on localized strings.

### Step 9: Preserve the future overlapped seam

Keep cancellation and operation state separate from the current synchronous implementation so a future `OVERLAPPED` backend can publish the same terminal result. Do not claim that this task implements full overlapped I/O.

### Step 10: Extend native serial tests

Add fakeable or deterministic assertions for state transitions, generation increments, close invalidation, deadline classification, short-write evidence, and exactly-once settlement in `tests/native_win32_serial_tests.cpp`.

### Step 11: Extend reconnect-state tests

Update `tests/native_reconnect_state_tests.cpp` to assert that reconnect preserves the requested options but never replays old request IDs or accepts stale-generation results.

### Step 12: Verify before caller migration

Build the session and focused tests while the main window still uses the transitional facade. Do not proceed to Tasks 3–5 until the owner can settle an old generation deterministically.

## Verification

- [ ] State transitions reject work in `closed`, `closing`, and `faulted` according to the contract.
- [ ] Every accepted request has one terminal result with the original generation and request ID.
- [ ] Reconnect increments generation and stale results cannot mutate the replacement session.
- [ ] Deadline and native error classification do not inspect localized message text.
- [ ] Close waits for worker settlement before releasing the handle or operation buffer.

**Test command:**

```bash
cmake --build build-windows-native-mingw --parallel 2
ctest --test-dir build-windows-native-mingw -R 'native_win32_serial_tests|native_reconnect_state_tests' --output-on-failure
```

**Expected output:**

```text
100% tests passed, 0 tests failed
```

## Commit

```text
feat: add generation-aware serial settlement (Phase 2, Task 2)
```
