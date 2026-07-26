# Task 4: Migrate Main-Window I/O

> Phase: 2 — Win32 Session and Caller Migration
> Status: Completed

---

## Objective

Move receive polling, manual/file enqueue, pending-result matching, cancellation, and queue presentation to generation-aware typed session operations without changing the existing send ownership or file-send UX.

## Files

**Create:**

- None

**Modify:**

- `src/win32/main_window.h`
- `src/win32/main_window_serial_io.cpp`
- `src/win32/main_window_send.cpp`
- `src/win32/native_serial_io_state.h`
- `src/win32/native_serial_io_state.cpp`

**Delete:**

- None

**Test:**

- `tests/native_serial_io_state_tests.cpp`

## Dependencies

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | `ReadFile`, `WriteFile`, `ClearCommError`, `WaitForSingleObject`, `SetEvent`, `CancelIoEx` | Exercise the session's byte operations and cancellation through typed capability calls; no UI source calls these APIs directly. |
| C++ standard library | None | `std::vector`, `std::deque`, `std::optional`, `std::find_if`, `std::uint64_t` | Match pending requests and aggregate receive batches without exposing backend state. |

## Steps

### Step 1: Extend pending-write identity

Update the pending UI write record in `main_window.h` to retain both session generation and request ID. Treat the pair as the only key for matching terminal results.

### Step 2: Route queue snapshots through the session capability

Update `updateSerialWriteQueueStatus` and `clearPendingSerialWrites` to use the typed queue snapshot and terminal-result drain operation. Do not clear the session queue by mutating backend members.

### Step 3: Match terminal results exactly once

Update `drainSerialWriteResults` to reject stale generations, remove a pending UI item only after its matching terminal result arrives, and preserve the existing manual/file success and failure handling.

### Step 4: Preserve UI-localized status

Make `writeResultMessage` map structured status/category to UI text. Do not use `lastErrorText` or any localized message as a branch condition.

### Step 5: Migrate receive polling

Update `pollSerial` to request a bounded read snapshot from the byte capability, handle structured timeout/disconnected/error outcomes, and keep raw evidence aggregation and payload logging unchanged.

### Step 6: Migrate manual enqueue

Update `enqueueManualSerialWrite` to submit a typed request with the current generation and explicit deadline. Record the returned request ID only after admission succeeds.

### Step 7: Migrate file enqueue and cancellation

Update `enqueueFileSerialWrite` and `stopFileSend` to submit chunked writes and cancel only requests owned by the current file-send generation. Preserve progress counters and file-send ownership release.

### Step 8: Preserve send-controller arbitration

Keep `NativeSerialSendController` and `NativeSerialIoState` responsible for ManualSend/FileSend ownership decisions. Change only the queue snapshot/result fields needed for generation and byte/deadline reporting.

### Step 9: Update queue-state unit tests

Extend `tests/native_serial_io_state_tests.cpp` for generation-aware pending counts, active request accounting, immediate rejection, and release behavior after terminal results.

### Step 10: Build and run I/O checkpoint

Build the native application and focused queue/UI-state tests while the temporary facade remains available for the not-yet-migrated Modbus path.

### Step 11: Audit stale-result behavior

Run a local fake sequence that closes and reopens between enqueue and completion. Confirm the old result is ignored by the UI and the new session's queue remains visible.

### Step 12: Keep lifecycle ownership unchanged

Do not add `CloseHandle`, `PurgeComm`, worker joins, or reconnect policy to these UI files; all such actions remain in `Win32SerialSession`.

## Verification

- [x] Pending writes match by `(generation, requestId)` and complete at most once.
- [x] Stale-generation results do not update status, file progress, history, raw evidence, or counters.
- [x] Manual send, timed send, file send, cancellation, receive polling, and queue display preserve current behavior.
- [x] UI files contain no direct Win32 serial API calls or native handle ownership.
- [x] Queue state tests cover active work, immediate rejection, and generation reset.

**Test command:**

```bash
cmake --build build-windows-native-mingw --parallel 2
ctest --test-dir build-windows-native-mingw -R 'native_serial_io_state_tests|native_win32_serial_tests' --output-on-failure
```

**Expected output:**

```text
100% tests passed, 0 tests failed
```

## Commit

```text
refactor: migrate generation-aware main-window serial I/O (Phase 2, Task 4)
```
