# Task 6: Remove Broad Transport Facade

> Phase: 2 — Win32 Session and Caller Migration
> Status: Completed

---

## Objective

Delete `SerialTransport` and all temporary compatibility inheritance, remove queue-owned capability inheritance and localized-error decision paths, and leave only the narrow session, byte, and write-scheduler contracts used directly by migrated callers.

## Files

**Create:**

- None

**Modify:**

- `src/transport/serial_session.h`
- `src/transport/serial_write_queue.h`
- `src/win32/win32_serial_session.h`
- `src/win32/win32_serial_session.cpp`
- `tests/transport_contract_tests.cpp`
- `tests/native_win32_serial_tests.cpp`
- `CMakeLists.txt`

**Delete:**

- `src/transport/serial_transport.h`

**Test:**

- `tests/transport_contract_tests.cpp`
- `tests/native_win32_serial_tests.cpp`

## Dependencies

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | `CreateFileW`, `CloseHandle`, `ReadFile`, `WriteFile`, `CancelIoEx`, `GetOverlappedResult`, `WaitForSingleObject`, `SetEvent`, `ClearCommError` | Keep the concrete session's native implementation behind narrow contracts while removing the broad C++ facade. |
| C++ standard library | None | `std::span`, `std::vector`, `std::optional`, `std::chrono`, `std::unique_ptr` | Express borrowed capabilities and structured results without a compatibility wrapper or third-party dependency. |

## Steps

### Step 1: Prove caller migration is complete

Run `rg` over `src`, `tests`, `CMakeLists.txt`, and the listed phase files. Confirm that no main-window, RTU, Modbus, or command caller still requires `SerialTransport`, `Win32SerialPort`, or the `serialTransport_` alias. If any caller remains, stop and return it to Tasks 3–5 rather than editing an out-of-scope file here.

### Step 2: Snapshot a green pre-removal build

Build all portable and MinGW targets and run the focused Phase 2 tests. Preserve the output as the before-removal baseline so a facade deletion failure is distinguishable from a migration failure.

### Step 3: Remove the temporary Win32 inheritance

Change `Win32SerialSession` so it implements or composes the narrow capabilities declared in `serial_session.h` directly. Remove `: public SerialTransport`, all broad `override` markers, and every adapter method that existed only to satisfy the old facade.

### Step 4: Remove queue-owned capability inheritance

Change `SerialWriteQueue` into a value-oriented queue implementation. Remove `: public SerialWritePort`, the inherited `enqueueWrite` override marker, and any queue method whose only purpose was to masquerade as a transport. Keep explicit queue operations needed by the session owner.

### Step 5: Narrow the session declarations

Update `serial_session.h` to contain only the lifecycle, byte-operation, write-scheduler, and result/event contracts required by the migrated callers. Do not add a `using SerialTransport` alias, forwarding class, typedef, or default adapter.

### Step 6: Remove localized-error decision paths

Delete broad `lastErrorText` decision methods from the session contract and implementation where no longer required. Ensure transport decisions use status/category/native code; leave UI localization to the already-migrated presentation boundary.

### Step 7: Delete the facade and includes

Delete `src/transport/serial_transport.h`, remove its CMake/test references, and update the two listed test files to include only the narrow contracts. Do not leave a compatibility header at the old path.

### Step 8: Update fake contract tests

Refactor `tests/transport_contract_tests.cpp` so fakes implement the narrow session/byte/write capabilities explicitly. Add assertions that no fake inherits the deleted facade and that queue ownership is composition, not inheritance.

### Step 9: Update native backend tests

Refactor `tests/native_win32_serial_tests.cpp` to construct the concrete session or narrow capability references directly. Preserve no-port, open-state, queue, control-line, and terminal-result assertions.

### Step 10: Refresh the CMake graph

Remove deleted-header assumptions and ensure all test targets include the new session headers through the existing `src` include path. Keep target names, link libraries, and test registration stable unless the deleted facade was listed explicitly.

### Step 11: Run structural audits

Require zero active references to `SerialTransport`, `Win32SerialPort`, `serialTransport_`, and the deleted header. Require that `SerialWriteQueue` has no capability inheritance and that no transport source branches on localized error text.

### Step 12: Rebuild the complete tree

Configure a clean portable build, build every MinGW target (not only the application), and run the full CTest set. Treat any missing include or vtable error as a failed facade removal, not as permission to restore a compatibility alias.

### Step 13: Hand off to Phase 3

Leave the tree with the old facade absent, the Win32 session as the sole concrete owner, and all callers using narrow capabilities. Queue byte-budget integration, race evidence, PTY expansion, and release gates remain Phase 3 work.

## Verification

- [x] `src/transport/serial_transport.h` is absent.
- [x] `rg -n "SerialTransport|Win32SerialPort|serialTransport_" src tests CMakeLists.txt` returns no output.
- [x] `rg -n "class SerialWriteQueue.*public|SerialWriteQueue.*override" src/transport/serial_write_queue.h` returns no output.
- [x] `rg -n "lastErrorText\(\)|lastErrorText_" src/transport src/win32` shows no transport decision API; UI text is derived from typed results.
- [x] No `using`, typedef, forwarding class, or temporary inheritance recreates the deleted facade.
- [x] Full portable CTest and MinGW/Wine native tests pass after a clean configure.

**Test command:**

```bash
cmake -S . -B /tmp/svm-transport-v2-phase2-final -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-phase2-final --parallel 2
ctest --test-dir /tmp/svm-transport-v2-phase2-final --output-on-failure
cmake --build build-windows-native-mingw --parallel 2
ctest --test-dir build-windows-native-mingw --output-on-failure
```

**Expected output:**

```text
100% tests passed, 0 tests failed
```

## Commit

```text
refactor: remove broad serial transport facade (Phase 2, Task 6)
```
