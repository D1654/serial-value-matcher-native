# Phase 3 Task 01 API Reference

## Scope

Task 01 adds production queue backpressure to the existing serial-only session:

- at most 64 counted requests;
- at most 256 KiB (`262144` bytes) of counted payload;
- pending and active work both remain counted;
- invalid or over-budget work is rejected immediately with a typed result;
- rejection does not allocate a request ID, reorder accepted work, or call native
  I/O;
- UI state consumes immutable queue snapshots rather than duplicating queue
  limits or counters.

This task does not change the synchronous Win32 write backend, add overlapped
I/O, or broaden in-flight cancellation guarantees. The Win32 research below
defines the completion boundary that queue accounting must preserve.

## Research Record

Five task-level DeepWiki questions were used, which is the configured maximum:

1. Windows write, cancellation, completion, byte-count, and resource lifetime:
   https://deepwiki.com/search/for-a-synchronous-or-overlappe_fc8a6d48-a55d-488f-bc60-887143dce6f5
2. CMake/CTest registration, filtering, output, and exit status:
   https://deepwiki.com/search/state-exact-cmake-and-ctest-be_e6baaa75-33d7-4eac-8c81-e51de23e28f4
3. Wine COM mapping, PTY boundaries, prefix isolation, and `wineboot`:
   https://deepwiki.com/search/explain-wine-comport-and-unix_73392844-3277-4b59-bc34-8f62de013442
4. Actions runner PowerShell exits, summaries, and artifacts:
   https://deepwiki.com/search/for-github-actions-powershell_5dd09a23-ea91-4dbd-be33-e90f57d0d2d3
5. Focused correction of overlapped `WriteFile` and cancellation settlement:
   https://deepwiki.com/search/resolve-this-exact-contract-wi_210cb288-5a3f-49a3-bba6-3afb50c4aa75

The first Windows answer contradicted itself about whether `WriteFile` returning
`TRUE` is terminal for an overlapped call. The fifth answer resolved the sample
pattern, and Microsoft Learn was used as the authoritative fallback for the API
contract. Official CMake and GitHub documentation was also used where exact
command or runner behavior matters. Wine repository source and this project's
PTY script define the Wine-specific boundary.

## Windows I/O Boundary

Primary references:

- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
- https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio
- https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations

Relevant signatures:

```cpp
BOOL WriteFile(
    HANDLE hFile,
    LPCVOID lpBuffer,
    DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten,
    LPOVERLAPPED lpOverlapped);

DWORD GetLastError();

BOOL GetOverlappedResult(
    HANDLE hFile,
    LPOVERLAPPED lpOverlapped,
    LPDWORD lpNumberOfBytesTransferred,
    BOOL bWait);

BOOL CancelIoEx(HANDLE hFile, LPOVERLAPPED lpOverlapped);
BOOL CancelSynchronousIo(HANDLE hThread);
```

### Completion and byte count

- Synchronous `WriteFile` returns only after success or failure. On success,
  `lpNumberOfBytesWritten` is the transferred count; the caller must still
  treat a successful short count as incomplete application work.
- For a handle opened with `FILE_FLAG_OVERLAPPED`, a valid, unique
  `OVERLAPPED` is required. A `FALSE` return with `ERROR_IO_PENDING` means the
  write is not terminal. It is not a failure result.
- For overlapped I/O, Microsoft recommends passing `NULL` for
  `lpNumberOfBytesWritten` and obtaining the authoritative count from
  `GetOverlappedResult` or the completion port. The documented Windows 7
  exception requires a non-null parameter but does not make its immediate value
  the final asynchronous count.
- `GetOverlappedResult(..., FALSE)` returns `FALSE` with
  `ERROR_IO_INCOMPLETE` while work is still pending. With `bWait == TRUE`, it
  waits for terminal completion and then reports the actual transferred count
  or a final error.
- `GetLastError` is per-thread and must be captured immediately after the API
  return value says it is meaningful. Logging, formatting, locking, or another
  Win32 call may overwrite it.

### Cancellation and resource lifetime

- `CancelIoEx(hFile, nullptr)` requests cancellation of outstanding I/O for the
  file handle across issuing threads in the current process. With a specific
  `OVERLAPPED`, it targets the matching request.
- `CancelSynchronousIo(hThread)` targets current synchronous I/O issued by that
  thread. It is not a file-handle-wide replacement for `CancelIoEx`.
- `ERROR_NOT_FOUND` means the cancellation API did not find a cancellable
  request. It does not establish whether a racing operation completed normally.
- A successful cancellation call is only a request. The original operation can
  still complete normally, complete as `ERROR_OPERATION_ABORTED`, or fail with
  another code. The caller must observe the original operation's terminal
  result.
- Until that terminal result, the device handle, write buffer, `OVERLAPPED`,
  event, and owning context remain alive and must not be reused.

Task 01 application:

- Admission and byte-budget arithmetic remain platform-neutral.
- Moving pending work to active work does not release its count or bytes.
- Cancellation request, native settlement, terminal publication, and budget
  release are distinct events. Budget is released exactly once at the existing
  terminal path, not when cancellation is merely requested.
- A queue snapshot reports owned work, not bytes that Windows has physically
  transmitted.
- `CancelIoEx`, `GetOverlappedResult`, and overlapped storage are future-backend
  constraints only; Task 01 must not add unused compatibility code for them.

## CMake and CTest Boundary

Primary references:

- https://cmake.org/cmake/help/latest/command/add_test.html
- https://cmake.org/cmake/help/latest/manual/ctest.1.html

Relevant syntax:

```cmake
add_test(NAME <name> COMMAND <command> [<arg>...])
```

```text
ctest --test-dir <path-to-build> --output-on-failure
ctest --test-dir <path-to-build> --output-on-failure -R <regex>
```

Exact behavior:

- CMake generates test metadata only after `enable_testing()`; including the
  `CTest` module normally enables it unless `BUILD_TESTING` is off.
- With the modern `add_test(NAME ... COMMAND <target>)` form, an executable
  target is replaced by its built path. When cross-compiling, its configured
  `CROSSCOMPILING_EMULATOR` is prepended according to the active CMake policy.
- `--test-dir` selects the generated build tree containing
  `CTestTestfile.cmake`; it does not configure or build that tree.
- `-R` runs only tests whose names match the regular expression. A focused run
  therefore proves only that selected subset.
- `--output-on-failure` reveals failed test output; it does not change pass/fail
  semantics.
- CTest exits zero when all executed tests pass and nonzero for a failed test or
  command error. A filter matching no tests is not necessarily an error unless
  `--no-tests=error` is also used.

Task 01 application:

- `native_serial_io_state_tests` is already explicitly registered, and the
  project sets a Wine emulator for cross-compiled Windows targets when found.
- Run the focused host and MinGW regexes for fast feedback, then run both full,
  unfiltered configured trees before acceptance.
- Build before CTest. A green host test cannot prove that the MinGW Win32 session
  compiled or ran.
- Do not change test registration merely to satisfy a focused filter; keep
  deterministic existing names and make failures visible through process exits.

## Wine COM and PTY Boundary

Repository references:

- https://github.com/wine-mirror/wine/blob/master/dlls/mountmgr.sys/device.c
- https://github.com/wine-mirror/wine/blob/master/dlls/ntdll/unix/serial.c
- https://github.com/wine-mirror/wine/blob/master/programs/wineboot/wineboot.c

Project source of truth:

- `scripts/run-windows-native-serial-pty-loopback.py`

Exact behavior and limitations:

- Wine prefix state, registry data, and DOS device mappings are scoped by
  `WINEPREFIX`. Wine serial implementation translates Windows serial controls
  to Unix `termios` and `ioctl`; the source itself records unsupported or
  approximated controls, so API equivalence is not complete.
- This project initializes a prefix with `wineboot -u` when
  `<prefix>/dosdevices/c:` is absent, rejects a nonzero `wineboot` exit, creates
  one `os.openpty()` pair, switches the slave to raw mode, and explicitly links
  `<prefix>/dosdevices/<lowercase-com-name>` to the PTY slave.
- The test process receives the same `WINEPREFIX` and opens the configured COM
  name. The Python harness exclusively owns the PTY master during the matrix.
- Prefix isolation and one exclusive PTY pair are project harness requirements,
  not guarantees supplied by a COM API. Reusing a dirty prefix or sharing a PTY
  can leak mappings, state, or traffic across runs.
- A passing matrix establishes the adapter contract through Wine and the host
  Unix serial layer for the named scenarios. It does not execute a Windows
  vendor USB-serial driver and cannot prove physical-device timing, unplug,
  modem-control, buffering, or driver cancellation behavior.

Task 01 application:

- Preserve the existing isolated-prefix and explicit-symlink harness; queue
  backpressure does not belong in Wine setup.
- Use deterministic `normal,reopen,timeout,cancel,stress` scenarios after unit
  tests, but label them local-only release-candidate evidence.
- Exact budget boundaries and checked arithmetic must be proved in deterministic
  unit tests. PTY stress is integration evidence, not the arithmetic oracle.

## GitHub Actions Boundary

Primary references:

- https://github.com/actions/runner/blob/main/src/Runner.Worker/Handlers/ScriptHandler.cs
- https://github.com/actions/runner/blob/main/src/Runner.Worker/FileCommandManager.cs
- https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax
- https://docs.github.com/en/actions/writing-workflows/choosing-what-your-workflow-does/workflow-commands-for-github-actions
- https://github.com/actions/upload-artifact

Exact behavior:

- The runner's `ScriptHandler` captures the shell process exit code and sets the
  step result to failed when it is nonzero.
- For the built-in `pwsh`/`powershell` shell, GitHub prepends
  `$ErrorActionPreference = 'stop'` and appends a check that exits with
  `$LASTEXITCODE` when that variable exists. A critical native command should
  still be the last command or have its exit checked immediately; later native
  commands can replace `$LASTEXITCODE`.
- Workflow `continue-on-error` can permit later execution despite a failed step;
  it is policy authored in YAML, not a successful command result.
- `GITHUB_STEP_SUMMARY` is a per-step Markdown evidence file. The runner skips a
  missing or empty file, limits a summary attachment to 1 MiB, and summary upload
  failures do not change the step or job status.
- `upload-artifact` is an action with its own policy. Its default behavior for no
  matching files is warning/success; `if-no-files-found: error` is required when
  absence must fail the action.
- Upload success proves that matched files were uploaded, not that their content
  contains the required gate result.

Task 01 application:

- Preserve nonzero propagation from configure, build, CTest, self-test, PTY,
  package, and documentation commands.
- Keep explicit existence, non-empty, and required-content assertions before
  artifact upload. Keep `if-no-files-found: error` as a second absence guard.
- Treat summaries and artifacts as diagnostics/evidence only. The authoritative
  gate is the producing command's exit plus explicit evidence validation.
- Task 01 does not require an Actions workflow change unless queue evidence is
  added to an existing required artifact contract.

## Integrated Task 01 Rules

1. Define the 64-request and 262144-byte limits once in the neutral queue/session
   policy; presentation code reads them from snapshots.
2. Reject empty, individually oversized, count-overflow, arithmetic-overflow, or
   aggregate-byte-overflow payloads before copying or assigning a request ID.
3. Count the active request until its single terminal release; pending-to-active
   transfer changes ownership state but not total counted work.
4. Release count and bytes through one audited terminal path for success,
   failure, timeout, cancellation, close, and disconnect.
5. Preserve FIFO order and leave queue state unchanged on rejection.
6. Publish coherent snapshots containing pending and total pressure, limits,
   active identity, and high-water evidence without exposing a native handle.
7. Make `NativeSerialIoState` consume typed status/category/snapshot data; never
   parse localized error text or reconstruct queue counters in the UI.
8. Verify arithmetic deterministically first, then Win32 integration, Wine PTY,
   full test trees, self-test/UI performance, package gates, and documentation.

## Research Confidence

- High: Win32 cancellation/completion lifetime, CMake/CTest selection and exit
  behavior, Actions shell and summary semantics.
- High for the checked-in harness: Wine prefix initialization, explicit COM
  symlink, PTY ownership, scenarios, and evidence classification.
- Medium for Wine versus physical devices: the repository proves a Unix
  translation layer, not vendor-driver equivalence. That limitation is retained
  explicitly and must not be converted into a stronger product claim.
