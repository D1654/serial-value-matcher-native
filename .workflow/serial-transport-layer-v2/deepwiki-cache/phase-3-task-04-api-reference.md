# Phase 3 Task 04 API Reference

Generated: 2026-07-20

## Research Record

Thirteen live DeepWiki `ask` calls completed for every dependency row in the
Task 04 plan:

| Repository | Queries | Coverage | Result |
|---|---:|---|---|
| `microsoft/Windows-classic-samples` | 4 | synchronous handle shutdown, `SetCommTimeouts`, `ClearCommError`, `CancelIoEx`/cancellation | The indexed sample gives a useful `CancelSynchronousIo` worker/join pattern, but has no direct serial `SetCommTimeouts` or `ClearCommError` example and does not establish `CancelIoEx` semantics for this synchronous backend. Official Microsoft documentation was required. |
| `Kitware/CMake` | 3 | `add_test`, cross-compiling emulator, focused/no-test behavior, optional-hardware skip | Completed with concrete CTest behavior. Official CMake documentation was also checked for version-sensitive details. |
| `wine-mirror/wine` | 3 | `WINEPREFIX`, `wineboot`, `dosdevices/comN`, serial timeout/cancel/close translation | Completed. The answer identified the Wine man page, `mountmgr.sys`, and Unix serial implementation. |
| `actions/runner` | 3 | native exit propagation, summaries, artifacts, local-only evidence wording | Completed. Content validation and the POSIX PTY policy are correctly identified as project workflow responsibilities, not runner-provided guarantees. |

No new runtime dependency is justified by this research.

## Source Fallbacks

DeepWiki explicitly lacked exact Windows serial API coverage, so the following
official sources were used as the authoritative fallback:

- Microsoft `CancelSynchronousIo`:
  <https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio>
- Microsoft `CancelIoEx`:
  <https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex>
- Microsoft cancellation guidance:
  <https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations>
- Microsoft `COMMTIMEOUTS`:
  <https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-commtimeouts>
- Microsoft `SetCommTimeouts`:
  <https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcommtimeouts>
- Microsoft `ClearCommError` and `COMSTAT`:
  <https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-clearcommerror>
  and <https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-comstat>
- Microsoft `ReadFile` and `WriteFile`:
  <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile>
  and <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile>

Official CMake pages were checked because `CROSSCOMPILING_EMULATOR` behavior is
policy/version sensitive:

- <https://cmake.org/cmake/help/latest/command/add_test.html>
- <https://cmake.org/cmake/help/latest/prop_tgt/CROSSCOMPILING_EMULATOR.html>
- <https://cmake.org/cmake/help/latest/policy/CMP0158.html>
- <https://cmake.org/cmake/help/latest/manual/ctest.1.html>
- <https://cmake.org/cmake/help/latest/prop_test/SKIP_RETURN_CODE.html>
- <https://cmake.org/cmake/help/latest/prop_test/TIMEOUT.html>

The Wine man page is the repository source for the `dosdevices/comN` mapping:
<https://github.com/wine-mirror/wine/blob/master/tools/wine/wine.man.in>.

## Exact Synchronous Win32 Contract

### Current backend, not a future overlapped backend

The checked-in session opens the COM device without `FILE_FLAG_OVERLAPPED`.
`ReadFile` and `WriteFile` are therefore synchronous calls. Microsoft directs
applications to `CancelSynchronousIo(threadHandle)` for pending synchronous I/O;
the current session already uses that API plus `PurgeComm` during worker stop.

The Task 04 dependency table names `CancelIoEx`, but Task 04 must not add a
second cancellation path merely to match that table. `CancelIoEx` is relevant to
a future overlapped backend. For this task it is research context only.

### Cancellation is a request, not settlement

- `CancelSynchronousIo` marks pending synchronous calls on the specified thread
  for cancellation and returns without waiting for them to finish.
- `ERROR_NOT_FOUND` means no matching pending synchronous request was found; it
  is not proof that a previous request was cancelled.
- The I/O can still complete normally, complete with
  `ERROR_OPERATION_ABORTED`, or fail with another native error.
- The worker thread and its payload buffer remain owned until the I/O call has
  returned and the worker has settled. Only then may the native handle be
  closed or the buffer reused.
- `PurgeComm(PURGE_TXABORT | PURGE_RXABORT)` is an additional driver request.
  It does not replace observing worker completion.
- Driver cancellation is not universally reliable. The existing bounded close
  result and retained-resource/fail-fast behavior must remain the honest limit.

The required ordering is therefore:

1. Mark the generation closing/invalid for new work.
2. Set the worker stop and terminal intent once.
3. Request synchronous cancellation and purge.
4. Wait within the existing bounded join budget.
5. Publish each accepted request exactly once.
6. Close the COM handle only after native settlement.

Task 04 tests this contract; it does not change the backend ordering.

### Serial timeouts

`SetCommTimeouts` replaces the timeout configuration for the communications
handle. A failure is a Win32 API failure and requires immediate
`GetLastError()` capture.

- Read total timeout is `multiplier * requestedBytes + constant`.
- Read interval timeout limits the gap between received bytes.
- Zero read multiplier and constant disable the total read timeout.
- Zero write multiplier and constant disable the total write timeout.
- `ReadIntervalTimeout == MAXDWORD` with both read total fields zero makes a
  read return immediately with currently buffered bytes, including zero bytes.
- A timed serial operation can complete successfully with fewer bytes than the
  caller originally hoped to receive. A timeout is not automatically a
  `ReadFile == FALSE` native failure.

The current `readAvailable` first calls `ClearCommError` and calls `ReadFile`
only when `cbInQue > 0`; it is a nonblocking availability poll. Consequently,
Task 04's response-timeout oracle must be the typed `SerialDeadline` result and
bounded process completion, not an assumption that a native `ReadFile` blocks.

### `ClearCommError`

- `FALSE` is API failure; capture `GetLastError()` immediately.
- On success, the `CE_*` value is a communications error mask, not a Win32
  last-error code.
- `COMSTAT::cbInQue` and `cbOutQue` are instantaneous driver queue observations.
  They are useful diagnostics but cannot prove I/O completion, cancellation,
  FIFO delivery, or handle settlement.

## CMake And CTest Contract

- The `add_test(NAME ... COMMAND <target>)` form resolves the executable target
  and prepends its `CROSSCOMPILING_EMULATOR` when applicable. The project uses
  this target form for both Win32 serial executables.
- CMake 3.29 policy `CMP0158` restricts emulator use to cross-compiling. Older
  CMake uses it unconditionally, but this project only sets
  `CMAKE_CROSSCOMPILING_EMULATOR` inside its explicit cross-compiling condition,
  so both behaviors run the MinGW tests through Wine as intended.
- A test normally passes only on exit code zero. Output text is diagnostic
  unless a PASS/FAIL/SKIP regex property is configured.
- `ctest -R` selects by test name. Use anchored names and
  `--no-tests=error` so a typo cannot produce a green empty run.
- A CTest `TIMEOUT` kills and fails a hung test. The Python PTY harness must also
  retain its own per-child timeout because it runs scenario processes directly.

The current no-port path prints an explicit skip message and returns zero. CTest
therefore records it as a pass, not `Not Run`. This satisfies the planned
"safely skipped or passed" rule and needs no CMake change. A true CTest skip
would require a distinct return code plus `SKIP_RETURN_CODE`, which is outside
the Task 04 file list and adds no behavioral safety here.

The no-port gate must still execute the named loopback test. It is not equivalent
to a regex that selected zero tests.

## Wine PTY Contract

- `WINEPREFIX` names the Wine data/configuration directory and its wineserver
  socket. Distinct prefixes provide independent registry, shared-memory, and
  DOS-device state.
- Initialize a new prefix first with bounded `wineboot`, then create or replace
  the test mapping immediately before launch:
  `$WINEPREFIX/dosdevices/com5 -> /dev/pts/N`.
- The Wine man page specifies lowercase `comN` symlink names pointing to Unix
  device files. The Windows program may still open `COM5`; DOS naming is
  case-insensitive at the Win32 boundary.
- Keep the PTY master descriptor open for the complete scenario. Closing it is
  endpoint-loss behavior, not a normal response timeout.
- `WINEARCH` is fixed when a prefix is created. Do not attempt to change it on
  an existing prefix.
- A unique prefix per evidence run is strongest isolation. If a caller
  deliberately reuses a prefix, the harness must replace the COM symlink and
  must not describe that run as clean-prefix evidence.

Wine translates Win32 serial operations onto Unix TTY behavior. A PTY can prove
the application's adapter contract under that translation. It cannot reproduce
vendor USB driver cancellation, framing/parity/overrun errors, hardware
RTS/CTS/DTR timing, USB unplug codes, or real-device latency.

## GitHub Actions Evidence Boundary

- The native child exit code and harness exit code are authoritative. A
  successful-looking log line or `GITHUB_STEP_SUMMARY` cannot turn a failed
  process into a passed step.
- `continue-on-error` changes the step conclusion policy and must not be used
  for a required gate.
- The runner and artifact uploader do not validate project-specific strings in
  an evidence file. Existence, nonempty content, `GateStatus`, and
  `Classification` must be asserted by project scripts before upload.
- The current Windows package workflow correctly writes
  `GateStatus=documented-local-only` and `CiExecutesPtyMatrix=no`. Task 04 must
  preserve that statement. It must not copy a developer's
  `GateStatus=passed` local summary into the Windows CI claim.
- Task 05 owns broader workflow/release-gate changes. Task 04 should only keep
  the local harness summary machine-readable and accurately classified.

## Actionable Scenario Design

The deterministic contract tests in `native_win32_serial_tests.cpp` remain the
authority for exact queue accounting, stale completion rejection, and
exactly-once publication. PTY scenarios add the real Wine/Win32 adapter path;
they must not duplicate those unit tests with weaker timing assumptions.

### `normal`

Keep the existing Modbus request and response bytes unchanged. Python must read
the exact request and send the exact response. The child validates request ID,
generation, byte counts, and response bytes.

### `reopen`

For every cycle, retain the opened generation, complete one or more normal
transactions, close, then reopen and require a strictly newer nonzero
generation. Before the first new transaction require an empty completion queue
and reset queue high-water marks. This proves no queued write replay.

It does not prove that arbitrary late serial bytes have a generation. Raw serial
bytes carry no provenance and the transport must not invent it.

### `timeout`

The child must first write the normal request. Python must observe the exact
request and deliberately withhold the response. The child polls
`readAvailable` with one bounded typed deadline and requires:

- terminal status `Timeout` and category `Timeout`;
- zero received bytes and zero successful transactions;
- the operation's current generation and assigned request ID;
- bounded child completion followed by a normal close.

This replaces the current weaker timeout path, which does not send a request.

### `cancel`

The public API cancels pending queued writes, not an arbitrary in-flight read.
The scenario name must retain that exact meaning.

Use one sufficiently large active write whose prefix is the normal Modbus
request, stop draining the PTY after Python observes that prefix, enqueue one
additional pending write, and call `cancelPendingWrites()`. Require exactly one
returned terminal result for the pending request with its original ID and
generation, status/category `Cancelled`, and no duplicate in
`takeCompletedWrites()`. Close then settles the active write separately.

Do not claim this proves physical-driver active-write cancellation.

### `close`

Use a large queued write with the normal request as its prefix and withhold PTY
drain after that prefix so Wine normally keeps the worker active. Wait for the
queue snapshot to name that active request, then call `close()` and require:

- bounded close completion;
- exactly one terminal result for the accepted active request;
- the old request ID and generation are retained;
- status is `Cancelled`/`SessionClosed`, or `Disconnected` only when actual
  endpoint loss was introduced;
- a second completion drain is empty and the queue is empty after successful
  native settlement;
- a post-close `readAvailable` is `RejectedClosed` with no bytes.

The last assertion is the honest "no late response accepted" contract. It does
not claim that the transport can identify the origin of bytes arriving after a
later reopen.

### `stale`

The PTY portion should perform old-generation close followed by a new-generation
normal transaction, asserting generation change, empty old completions, and no
replay. The precise stale-completion proof belongs in
`native_win32_serial_tests.cpp`: inject an old `(requestId, generation)`
completion after replacement generation setup and require that it cannot release
the new active reservation or publish a result.

Do not send an identical "late response" and label its rejection as a transport
generation guarantee. Serial bytes do not include a generation tag; frame/value
matching above transport owns that policy.

### `pressure`

The deterministic limit proof should continue using test access in
`native_win32_serial_tests.cpp` for the exact 64-request and 256-KiB accounting,
including the active reservation. The PTY scenario may add public-path
integration by keeping one large write active, filling pending admissions, and
requiring the next admission to return immediately as `RejectedFull` without
changing the prior request IDs or high-water snapshot.

Because whether a Unix PTY blocks a particular large `WriteFile` is a Wine/host
scheduling fact, the PTY pressure result is supporting adapter evidence. The
neutral queue test remains the authoritative capacity proof.

### `stress`

Retain explicit iteration and reopen controls. Python verifies every FIFO
request before sending its response. The child and harness must agree on the
transaction count, and process timeout must scale from a bounded base plus a
bounded per-transaction allowance.

### No-port

Run `native_win32_serial_loopback_tests` without
`SVM_NATIVE_SERIAL_LOOPBACK_PORT`. Require the explicit skip line, exit zero,
and bounded completion. Also run `native_win32_serial_tests`; neither path may
open an arbitrary host COM port.

## Harness Controls And Failure Evidence

Add only bounded positive-integer controls that are consumed by a scenario,
such as close settle delay, stale/reopen iterations, and pressure payload/count.
Reject unknown scenarios, duplicates if they would distort summary counts, zero,
negative, malformed, and over-maximum values before creating a PTY or Wine
child.

Every failure must include at least:

- scenario name;
- transaction/request index where applicable;
- expected and observed request bytes for wire mismatches;
- child return code or process-timeout marker;
- COM name, but never payload bytes beyond the bounded fixture diagnostic.

The summary is written only after all selected scenarios pass. It must retain:

```text
GateStatus=passed
Classification=local-only-release-candidate-evidence
Transport=serial-adapter-contract
```

It should additionally record the selected scenarios and transaction count. The
PTY path and Wine prefix are useful local diagnostics, not portable proof and
must not be promoted into a CI coverage claim.

## Focused Verification Guidance

Build the MinGW executables before selecting tests. Use anchored focused names
and fail an empty selection:

```bash
ctest --test-dir build-windows-native-mingw --output-on-failure --no-tests=error \
  -R "^(native_win32_serial_tests|native_win32_serial_loopback_tests|native_reconnect_state_tests)$"
```

The CTest loopback run is the no-port safety path. The Python harness separately
runs the selected PTY scenarios with explicit child timeouts. A full unfiltered
MinGW CTest tree remains required after the focused run.

## Exclusions

- No TCP, Qt, new runtime dependency, overlapped-I/O migration, or compatibility
  wrapper.
- No direct `CancelIoEx` call in the current synchronous backend tests.
- No assumption that cancel request means native settlement.
- No claim that PTY results establish physical USB-serial behavior.
- No invented generation tag for raw serial bytes.
- No CI `GateStatus=passed` claim for a POSIX PTY scenario that CI did not run.
