# Task 3: Migrate Main-Window Lifecycle

> Phase: 2 — Win32 Session and Caller Migration
> Status: pending

---

## Objective

Move connection lifecycle, line-control, endpoint, reconnect decisions, and persisted serial-profile access to the typed `Win32SerialSession` while keeping polling and queued-write migration isolated for Task 4.

## Files

**Create:**

- None

**Modify:**

- `src/win32/main_window.h`
- `src/win32/main_window_serial.cpp`
- `src/win32/main_window_commands.cpp`
- `src/win32/main_window_storage.cpp`

**Delete:**

- None

**Test:**

- Existing native main-window self-test coverage invoked through the build and release checks.

## Dependencies

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | `SendMessageW`, `SetTimer`, `KillTimer`, `EscapeCommFunction`, `GetCommState`, `SetCommState`, `SetCommTimeouts`, `CloseHandle` | Keep UI command dispatch and serial line-control behavior while routing all lifecycle actions to the one session owner. |
| C++ standard library | None | `std::optional`, `std::string`, `std::wstring`, `std::vector` | Carry typed open options, endpoint snapshots, and reconnect state without introducing another framework. |

## Steps

### Step 1: Identify lifecycle call sites

List every `open`, `close`, `isOpen`, `endpoint`, `setDataTerminalReady`, `setRequestToSend`, and reconnect call in the four listed source files. Leave polling, enqueue, and completed-write calls for Task 4.

### Step 2: Bind the owner field

Make `NativeMainWindow` hold one `Win32SerialSession` concrete owner and expose only the Phase 1 lifecycle capability to these methods. Keep any temporary facade reference only for untouched I/O call sites and bind it to the same object.

### Step 3: Route connect through typed results

Update `connectSerial` to validate options, call the session open operation, record the returned generation, and display a UI-localized failure without parsing a transport error string for control flow.

### Step 4: Route disconnect through settlement

Update `closeSerialPort` and `shutdownSerialPort` to request session close, wait for the session's settlement guarantee, clear UI timers, and then update presentation state. Do not independently close or purge a native handle in the window.

### Step 5: Preserve reconnect policy ownership

Keep `NativeReconnectState` responsible for whether to retry and which options to use. Have the session own generation invalidation and old-request settlement; do not replay old request IDs during `tryAutoReconnect`.

### Step 6: Migrate line controls

Route DTR and RTS changes through the lifecycle capability. Preserve hardware RTS/CTS rejection, UI checkbox rollback, and next-connection behavior while using the session's structured status.

### Step 7: Migrate endpoint snapshots

Use an immutable endpoint snapshot from the session for connection logs, disconnect logs, reconnect status, and raw-evidence metadata. Do not expose the native handle or mutable options object.

### Step 8: Migrate persisted profile access

Keep `currentOpenOptions` and `saveCurrentSerialProfile` responsible for user-selected configuration, but make stored endpoint and control-line values read from typed session/options values rather than concrete port internals.

### Step 9: Update command dispatch references

Change `main_window_commands.cpp` only where it decides connection, line-control, or flow-control lifecycle behavior. Leave send and queue commands to Task 4.

### Step 10: Run the lifecycle checkpoint

Build the application and run the existing native self-test plus reconnect and connection-state tests. Confirm that the UI still owns presentation state and the session alone owns native lifecycle.

## Verification

- [ ] `rg -n "CreateFile|CloseHandle|EscapeCommFunction|SetCommState|SetCommTimeouts" src/win32/main_window*.cpp` finds no direct native handle lifecycle call.
- [ ] `rg -n "serialTransport_|Win32SerialPort" src/win32/main_window_serial.cpp src/win32/main_window_commands.cpp src/win32/main_window_storage.cpp` shows no concrete backend dependency for lifecycle decisions.
- [ ] Connect, disconnect, DTR/RTS, hardware-flow-control, and auto-reconnect self-test paths pass.
- [ ] The temporary facade reference, if still present, points to the same `Win32SerialSession` object and is not a second owner.

**Test command:**

```bash
cmake --build build-windows-native-mingw --parallel 2
ctest --test-dir build-windows-native-mingw -R 'native_connection_ui_state_tests|native_reconnect_state_tests|native_win32_serial_tests' --output-on-failure
```

**Expected output:**

```text
100% tests passed, 0 tests failed
```

## Commit

```text
refactor: migrate main-window serial lifecycle (Phase 2, Task 3)
```
