# Task 5: Migrate RTU and Modbus Borrowing

> Phase: 2 — Win32 Session and Caller Migration
> Status: pending

---

## Objective

Make Modbus RTU borrow only the byte capability, capture the session generation for each scan, and suppress stale worker results after close or reconnect without changing RTU framing policy.

## Files

**Create:**

- None

**Modify:**

- `src/transport/serial_rtu_transport.h`
- `src/transport/serial_rtu_transport.cpp`
- `src/win32/native_modbus_scan_worker.h`
- `src/win32/native_modbus_scan_worker.cpp`
- `src/win32/main_window_modbus.cpp`

**Delete:**

- None

**Test:**

- `tests/native_modbus_transport_adapter_tests.cpp`
- `tests/native_protocol_modbus_tests.cpp`

## Dependencies

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | `ReadFile`, `WriteFile`, `ClearCommError`, `WaitForSingleObject`, `CancelIoEx`, `CloseHandle` | The session performs byte I/O and cancellation; the RTU adapter consumes results without owning a HANDLE. |
| C++ standard library | None | `std::chrono`, `std::function`, `std::vector`, `std::atomic`, `std::uint64_t` | Carry generation, cancellation, deadlines, and frame bytes across the worker boundary. |

## Steps

### Step 1: Inspect the Phase 1 byte capability

Confirm that `SerialRtuTransport` can request byte writes and bounded reads without depending on lifecycle, queue, UI, or Win32 concrete types. Keep the adapter's existing exchange callback and timeout text boundary.

### Step 2: Replace broad transport references

Change `serial_rtu_transport.h/.cpp` to depend on the Phase 1 byte capability and typed operation result. Do not add a queue or handle member to the adapter.

### Step 3: Capture the worker generation

Extend the scan request/context in `native_modbus_scan_worker.h` to carry the generation captured when the scan starts. Keep cancellation ownership in the existing atomic request path.

### Step 4: Validate generation before each exchange

In `native_modbus_scan_worker.cpp`, reject or terminate the scan when the session generation no longer matches the captured value. Classify that termination as cancellation/disconnection rather than a protocol failure.

### Step 5: Preserve response assembly

Keep current expected-length accumulation, callback ordering, and RTU frame bytes. Do not add CRC, stale-RX purge, inter-byte timing, retry, or device-specific decoding policy in this task.

### Step 6: Route the main-window worker setup

Update `main_window_modbus.cpp` to borrow the byte capability and generation snapshot from the session. Ensure the window does not pass a concrete port pointer or transfer HANDLE ownership to the worker.

### Step 7: Settle worker shutdown

Make the existing cancel/join path wait for the worker's terminal result before allowing disconnect completion. Preserve the current `disconnectAfterModbusScan_` behavior and UI ownership arbitration.

### Step 8: Update adapter tests

Extend `tests/native_modbus_transport_adapter_tests.cpp` for chunked reads, write failure, timeout, cancellation, and a stale-generation result that cannot be delivered as a valid frame.

### Step 9: Update protocol executor tests

Adjust `tests/native_protocol_modbus_tests.cpp` fakes and assertions so protocol execution depends only on the RTU exchange contract and remains independent of session lifecycle and Win32 types.

### Step 10: Build the Modbus path

Build the shared core and native worker targets before the final facade removal. Run focused adapter and protocol tests while the temporary transport bridge is still present.

### Step 11: Audit the boundary

Run `rg` over the RTU and Modbus files and confirm there are no `HANDLE`, `Win32SerialPort`, `SerialTransport`, queue mutation, or UI-window ownership references.

### Step 12: Record deferred framing work

Leave a planning note in the task result that malformed/late/stale RTU frame policy remains a later protocol-layer decision; do not change behavior silently.

## Verification

- [ ] RTU adapter compiles against the byte capability without a Win32 include.
- [ ] Modbus worker carries and checks generation, and stale results never update a replacement session.
- [ ] Existing normal, chunked, timeout, cancellation, and write-failure tests pass.
- [ ] RTU response assembly and callback ordering remain unchanged.
- [ ] No Modbus/RTU file owns a handle, queue, reconnect policy, or UI state.

**Test command:**

```bash
cmake --build build-windows-native-mingw --parallel 2
ctest --test-dir build-windows-native-mingw -R 'native_modbus_transport_adapter_tests|native_protocol_modbus_tests|native_modbus_scan_request_tests|native_modbus_scan_ui_state_tests' --output-on-failure
```

**Expected output:**

```text
100% tests passed, 0 tests failed
```

## Commit

```text
refactor: migrate Modbus borrowing to byte capability (Phase 2, Task 5)
```
