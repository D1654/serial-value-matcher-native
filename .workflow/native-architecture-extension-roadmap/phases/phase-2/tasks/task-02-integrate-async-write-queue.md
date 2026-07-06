# Task 02: Integrate Async Write Queue

> Phase: 2 — Backend Consistency
> Status: pending

---

## Objective

Route manual and file/batch send paths through the non-blocking bounded serial write queue.

## Files

**Create:**
- None

**Modify:**
- `src/win32/main_window_serial_io.cpp`
- `src/win32/main_window_send.cpp`
- `src/win32/win32_serial_port.h`
- `src/win32/win32_serial_port.cpp`
- `src/transport/serial_write_queue.h`
- `src/transport/serial_write_queue.cpp`
- `tests/native_win32_serial_tests.cpp`
- `tests/native_file_send_state_tests.cpp`

**Test:**
- `tests/native_win32_serial_tests.cpp`
- `tests/native_file_send_state_tests.cpp`
- `tests/serial_write_queue_tests.cpp`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| Win32 API | microsoft/Windows-classic-samples | `WriteFile`, overlapped I/O references | Keep native serial writes off UI wait paths. |
| Qt SerialPort | qt/qtserialport | timeout/error behavior reference only | Compare error semantics without adding runtime dependency. |

## Steps

### Step 1: Trace Current Write Paths

Identify all synchronous write calls from manual send, file send, and native serial UI code.

### Step 2: Route Writes Through Queue

Replace direct UI-thread write waits with queue submission and result events.

### Step 3: Surface Write Results

Update UI state/logging for accepted, sent, failed, timeout, cancelled, and backpressure outcomes.

### Step 4: Preserve Manual Send UX

Ensure single-send still gives immediate feedback and clear failure messages.

### Step 5: Add Regression Tests

Cover manual send, file send, timeout, cancellation, and queue full behavior.

### Step 6: Split If Scope Grows

If implementation touches more live paths than expected, split before further coding.

## Verification

- [ ] UI thread no longer synchronously waits for serial write completion.
- [ ] Manual send result is visible and logged.
- [ ] Focused and full tests pass.

**Test command:**
```bash
ctest --test-dir build-codex --output-on-failure -R "serial_write_queue|native_win32_serial|native_file_send_state"
```

**Expected output:**
```
100% tests passed
```

## Commit

```
feat: integrate async native serial write queue (Phase 2, Task 02)
```
