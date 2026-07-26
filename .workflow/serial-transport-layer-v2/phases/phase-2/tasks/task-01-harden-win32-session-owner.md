# Task 1: Harden Win32 Session Owner

> Phase: 2 — Win32 Session and Caller Migration
> Status: Completed

---

## Objective

Rename the concrete Win32 backend to `Win32SerialSession` and make it the sole move-disabled owner of the COM handle, serial configuration, control lines, worker state, and queue while keeping the existing callers buildable until the final facade-removal task.

## Files

**Create:**

- `src/win32/win32_serial_session.h`
- `src/win32/win32_serial_session.cpp`

**Modify:**

- `CMakeLists.txt`
- `src/win32/main_window.h`
- `tests/native_win32_serial_tests.cpp`

**Delete:**

- `src/win32/win32_serial_port.h`
- `src/win32/win32_serial_port.cpp`

**Test:**

- `tests/native_win32_serial_tests.cpp`

## Dependencies

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | `CreateFileW`, `CloseHandle`, `SetupComm`, `PurgeComm`, `GetCommState`, `SetCommState`, `SetCommTimeouts`, `EscapeCommFunction`, `InitializeCriticalSection`, `DeleteCriticalSection`, `CreateEvent`, `CreateThread`, `WaitForSingleObject` | Preserve the existing COM resource setup and make all native resource ownership private to the session. |
| C++ standard library | None | `std::vector`, `std::deque`, `std::string`, `std::optional`, move/delete special members | Preserve value ownership and non-copyable session semantics without a new dependency. |

## Steps

### Step 1: Confirm the Phase 1 capability signatures

Read `src/transport/serial_session.h`, `src/transport/serial_types.h`, and `src/transport/serial_write_queue.h` and record the exact methods that the renamed backend must satisfy. Do not add a second capability or compatibility header.

### Step 2: Create the session declaration

Create `Win32SerialSession` in `src/win32/win32_serial_session.h` with the current open, close, read, write, queue, control-line, endpoint, and diagnostic operations. Keep the public API identical to the Phase 1 transitional contract so callers remain compilable.

### Step 3: Move resource members into the session

Move the handle, validated options, error evidence, critical section, wake event, worker handle, write queue, completed-result deque, stop flag, and in-progress flag from the deleted port declaration into the new session declaration.

### Step 4: Preserve ownership restrictions

Delete copy construction and copy assignment, delete move construction and move assignment, keep the destructor responsible for shutdown, and expose no native `HANDLE` or internal buffer to callers.

### Step 5: Move implementation helpers

Move the existing UTF-8 conversion, DCB/timeouts configuration, control-line setup, synchronous byte I/O, write-worker procedure, queue completion, read polling, and error-text helpers into `win32_serial_session.cpp` without changing behavior.

### Step 6: Keep transitional facade inheritance explicit

Until Task 6, retain only the minimum temporary inheritance or method surface required by the existing `SerialTransport` callers. Mark this as a migration bridge in the task diff and do not add any forwarding type alias or second concrete owner.

### Step 7: Switch the CMake source list

Replace the deleted `win32_serial_port` source paths with the two `win32_serial_session` paths in `CMakeLists.txt`. Keep target names and link libraries unchanged.

### Step 8: Rename the main-window owner field

Change `src/win32/main_window.h` to store `Win32SerialSession` as the concrete owner. Keep any remaining transitional reference bound to that same object; do not instantiate a second transport.

### Step 9: Update the native serial test type references

Change `tests/native_win32_serial_tests.cpp` to include and exercise `Win32SerialSession`, preserving the no-port failure and queue behavior assertions. Add a compile-time assertion that the new type is non-copyable.

### Step 10: Build the native target before running tests

Configure or refresh the MinGW build and build the application plus native serial test target. Stop if the old source names or a duplicate owner remain in the target graph.

### Step 11: Run focused regression tests

Run the native serial test and the portable transport tests that compile the shared headers. Confirm that the rename has not changed result status or queue behavior.

### Step 12: Record the handoff invariant

Leave the working tree with exactly one concrete session object in the main window and with the temporary facade bridge isolated for Task 6 removal.

## Verification

- [x] `rg -n "win32_serial_port|Win32SerialPort" CMakeLists.txt src tests` returns no active source or target reference.
- [x] `rg -n "Win32SerialSession" CMakeLists.txt src/win32 tests/native_win32_serial_tests.cpp` shows the owner, implementation, and test references.
- [x] `cmake --build build-windows-native-mingw --parallel 2` completes without duplicate backend symbols.
- [x] `native_win32_serial_tests` passes its no-port and queue assertions.
- [x] The session is non-copyable and no public method returns a native handle.

**Test command:**

```bash
cmake --build build-windows-native-mingw --parallel 2
ctest --test-dir build-windows-native-mingw -R 'native_win32_serial_tests|transport_contract_tests' --output-on-failure
```

**Expected output:**

```text
100% tests passed, 0 tests failed
```

## Commit

```text
refactor: establish Win32 serial session owner (Phase 2, Task 1)
```
