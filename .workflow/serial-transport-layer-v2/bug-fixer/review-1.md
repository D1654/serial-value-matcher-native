# Bug Fixer Review 1 - Serial Transport v2 Post-Completion Audit

- Workflow: `serial-transport-layer-v2`
- Trigger: user-requested post-completion review
- Target: transport v2 core, migrated callers, serial evidence/storage boundaries,
  and release verification path
- Pre-workflow baseline: `767310b27686314ba58f41a055c29954e6e7e945`
- Final implementation: `2f694804fd13370128d41752d74f5de07d702d4a`
- Reviewed closure commit: `f2f5377bcf47273964295ac16c1dc4b02317aad9`
- Completed: `2026-07-25T20:29:13+08:00`
- Fix completed: `2026-07-26T16:55:09+08:00`
- Fix status: all approved findings H-1, H-2, M-1 through M-13, and L-1
  through L-6 fixed; no review finding remains open

## Summary

The approved 18-task workflow is complete and its final build/test/package gates
are green. The review did not find a regression in queue count/byte accounting,
request ID monotonicity, exactly-once write finalization, generation isolation,
or the removal of Qt and the broad transport facade.

The post-completion audit found 21 actionable issues:

- Critical: 0
- High: 2
- Medium: 13
- Low: 6

The user approved all 21 findings across five fix passes. All 21 were fixed and
verified:

| Severity | Found | Fixed | Remaining |
|---|---:|---:|---:|
| Critical | 0 | 0 | 0 |
| High | 2 | 2 | 0 |
| Medium | 13 | 13 | 0 |
| Low | 6 | 6 | 0 |
| Total | 21 | 21 | 0 |

The two high findings, all thirteen medium findings, and all six low findings
are fixed. Package reinspection is cross-platform, PTY evidence names only the
behavior it actually proves, and the completed workflow records are closed.

## Review Method

- Tier 1 screened all 254 paths changed between the pre-workflow baseline and
  final implementation, including 181 product, test, script, workflow, and build
  paths.
- Tier 2 used focused reads for the transport contracts, write queue, RTU
  adapter, Win32 session owner, main-window serial/Modbus callers, command
  sequence, native evidence/storage path, tests, package scripts, and workflows.
- Tier 3 traced confirmed lifecycle, framing, persistence, and release-evidence
  findings through their callers and tests.
- Three independent review passes and two independent finding-verification passes
  were used to reduce false positives.

## Dimension Results

| Dimension | Result |
|---|---|
| Security | Evidence-directory ownership, Win32 device-path validation, and package staging containment are fixed. No credential, network, or new dependency exposure found. |
| Logic | Repeated open, RTU assembly, close publication, final Modbus retry classification, and delay semantics are fixed. |
| Concurrency | The queued write worker, generation checks, bounded Modbus shutdown, and deferred batch-failure settlement are sound in reviewed paths. Every registered test and both Windows jobs now have explicit finite timeouts. |
| Performance | No confirmed hot-path regression. Tiered CTest and workflow timeouts now bound deadlock regressions without tightening normal successful runs. |
| Error handling | Grouped storage recovery, first-write transactions, evidence export, persistence-failure visibility, and native build/release evidence handling are fixed. |
| Dependencies | Pass. No new runtime library was added; GitHub Actions remain pinned by exact SHA. |
| Consistency | PTY coverage claims, reusable self-test evidence, main-branch UI evidence binding, scenario naming, and all workflow plan states are consistent with the verified implementation. |

## High Findings

### H-1 Multi-file native-store recovery can silently mix transaction versions

- Dimensions: error handling, consistency
- Status: fixed
- Fix applied: added one group transaction manifest and atomic commit marker;
  recovery now rolls the full target set back before commit or forward after
  commit, including legacy per-file artifact recovery before a grouped save.
- Files:
  - `src/native_storage/native_store_file_ops.cpp:83`
  - `src/native_storage/native_store_file_ops.cpp:242`
- Problem:
  - A multi-file rewrite installs all new files and then deletes each `.bak`
    separately.
  - Recovery also decides independently per file: a remaining `.bak` causes that
    file to roll back, while a file whose backup was already deleted stays new.
  - A crash or backup-cleanup failure between deletions can therefore retain a
    new parent file while rolling child files back, or the reverse.
- Impact:
  - Scan, match, or verification records can reopen as a silently mixed version.
  - Existing injected replacement-failure tests cover rollback before commit,
    not the post-install backup-cleanup window.
- Suggested fix:
  - Add one transaction manifest and an atomic commit marker for the complete
    target set. Recovery must commit or roll back the set as one unit.
- Origin note: this is a pre-existing adjacent native-storage risk, not introduced
  by the transport commits, but it affects persisted serial scan integrity.

### H-2 Evidence export can delete unrelated user files and leave a partial bundle

- Dimensions: security, error handling
- Status: fixed
- Fix applied: export now requires a new dedicated child directory, writes and
  validates the complete bundle in a unique sibling temporary directory, and
  publishes it with one directory rename without deleting fixed names from a
  user-selected directory.
- Files:
  - `src/win32/main_window_analysis.cpp:230`
  - `src/report/evidence_bundle_writer.cpp:283`
  - `src/report/evidence_bundle_writer.cpp:397`
- Problem:
  - The UI accepts an arbitrary selected directory.
  - Export unconditionally deletes six fixed names, including `summary.md`, from
    that directory without proving the directory is owned by a previous bundle.
  - It then writes the replacement files directly and sequentially.
- Impact:
  - Selecting Desktop, a project directory, or another non-empty directory can
    delete unrelated same-named files.
  - A later write failure or crash leaves the previous bundle gone and the new
    bundle incomplete.
- Suggested fix:
  - Export into a dedicated new subdirectory or validated bundle directory.
  - Write to a sibling temporary directory, validate the full set, then perform
    one atomic directory-level switch where supported.

## Medium Findings

### M-1 Production repeated open violates the session contract

- Dimensions: logic, lifecycle consistency
- Status: fixed
- Fix applied: production `open()` now rejects every non-`Closed` state without
  closing the active session, changing its generation, or disturbing queued work.
- Files:
  - `src/win32/win32_serial_session.cpp:267`
  - `tests/transport_contract_tests.cpp:73`
  - `tests/transport_contract_tests.cpp:722`
- Problem:
  - The contract fake rejects `open()` unless the state is `Closed` and preserves
    the active generation on a duplicate call.
  - `Win32SerialSession::open()` instead calls `close()` for every non-closed
    state, then attempts the new open.
- Impact:
  - A repeated or invalid open can cancel accepted writes, discard a healthy
    connection, advance the generation, and then fail into `Faulted`.
  - The current connect button guards this path, but the production capability
    itself is not safe for other or future callers.
- Suggested fix:
  - Reject non-`Closed` open requests with the typed invalid-state result. Remove
    the implicit close/reopen compatibility behavior and add a production-backend
    regression test.

### M-2 RTU response assembly no longer follows the response frame boundary

- Dimensions: logic, consistency, testing
- Status: fixed
- Fix applied: response headers now select exception and normal frame lengths,
  exactly one frame is returned, unknown functions use incremental CRC-prefix
  scanning, and accumulated input is capped at the 260-byte RTU maximum.
- Files:
  - `src/transport/serial_rtu_transport.cpp:17`
  - `src/transport/serial_rtu_transport.cpp:28`
  - `src/transport/serial_rtu_transport.cpp:190`
  - `src/transport/serial_rtu_transport.cpp:207`
  - `tests/native_modbus_transport_adapter_tests.cpp:127`
  - `.workflow/serial-transport-layer-v2/phases/phase-2/tasks/task-05-migrate-rtu-and-modbus-borrowing.md:60`
- Problem:
  - Completion length is derived only from the request quantity, not the response
    function and byte-count header.
  - A completed response is returned with the entire accumulated buffer instead
    of exactly one frame.
  - The previous Qt adapter returned exception frames at 5 bytes, normal frames
    at `5 + response[2]`, and truncated to the selected frame boundary.
- Impact:
  - A wrong-function or short/malformed response can be mislabeled as timeout
    instead of reaching the parser as a protocol error.
  - A valid response followed by stale/trailing bytes can fail CRC because the
    parser receives both frames as one.
- Suggested fix:
  - Restore response-header-driven frame length and return exactly one assembled
    frame. Add wrong-function, byte-count mismatch, partial, concatenated, and
    trailing-byte tests. Keep CRC and stale-RX policy above the byte session.

### M-3 Modbus shutdown can wait forever on a blocked synchronous driver call

- Dimensions: concurrency, error handling
- Status: fixed
- Fix applied: cancellation now targets the Modbus worker's synchronous I/O,
  shutdown uses a bounded join, and process teardown fails fast rather than
  releasing live worker resources after the bound expires.
- Files:
  - `src/win32/main_window_messages.cpp:133`
  - `src/win32/main_window_modbus.cpp:271`
  - `src/win32/native_modbus_scan_worker.cpp:241`
  - `src/transport/serial_rtu_transport.cpp:66`
- Problem:
  - Window destruction requests cancellation and immediately joins the Modbus
    worker with `WaitForSingleObject(..., INFINITE)`.
  - The worker performs synchronous byte-stream calls. The queue-worker
    `CancelSynchronousIo`/`PurgeComm` path does not target this thread, and serial
    shutdown happens only after the join.
- Impact:
  - Normal drivers usually return within configured deadlines, but a stuck USB
    serial driver can freeze the UI and prevent process shutdown indefinitely.
- Suggested fix:
  - Add a session-owned cancellation path for direct byte I/O, use a bounded join,
    and retain resources or fail fast if native settlement cannot be obtained.

### M-4 Close settlement bypasses the normal stale-generation publication gate

- Dimensions: concurrency, logic, consistency
- Status: fixed
- Fix applied: close settlement publishes only exact successful write results
  from the closing generation; normal drain processes a complete result batch
  before invoking one deferred failure close, and a prior success from the exact
  faulted generation remains publishable when a later result faults the session.
- Files:
  - `src/win32/main_window_serial.cpp:207`
  - `src/win32/main_window_serial_io.cpp:34`
  - `src/win32/main_window_serial_io.cpp:120`
  - `.workflow/serial-transport-layer-v2/phases/phase-2/tasks/task-04-migrate-main-window-io.md:83`
- Problem:
  - `closeSerialPort()` closes the session before `clearPendingSerialWrites()`.
  - The clear path publishes any matching success to raw evidence, send history,
    payload log, and Tx counters without applying the normal current-generation
    decision.
  - The ordinary drain path also persists metadata for unknown or explicitly
    ignored stale results.
- Impact:
  - A write that physically finished at the close boundary can update UI-local
    history/counters after the generation is invalidated, contrary to the task's
    recorded stale-result acceptance criterion.
  - Exact `(generation, requestId)` matching prevents a new request from being
    mistaken for the old one; this is evidence/state semantics, not wire replay.
- Suggested fix:
  - Define the intended close-boundary evidence rule explicitly. Route both drain
    and close settlement through one generation-aware decision and test close
    racing a successful active write.

### M-5 Serial validation accepts arbitrary Win32 device namespace paths

- Dimensions: security, input validation
- Status: fixed
- Fix applied: validation accepts only an optional Win32 device prefix followed
  by `COM[1-9][0-9]*`, then reconstructs the canonical `\\.\COMn` path.
- Files:
  - `src/win32/win32_serial_types.cpp:88`
  - `src/win32/win32_serial_types.cpp:154`
  - `src/win32/win32_serial_session.cpp:309`
- Problem:
  - Any value beginning with `\\.\` or `\\?\` passes validation and reaches
    `CreateFileW` with read/write access, although the error text promises COM
    names only.
- Impact:
  - A direct caller or tampered local configuration can attempt to open a raw
    device or named pipe. The dropdown-only UI and later `SetupComm` failure
    reduce the current exposure but do not make the core API correct.
- Suggested fix:
  - Strip the optional device prefix, validate only `COM[1-9][0-9]*`, then rebuild
    the canonical path. Add negative raw-disk, pipe, and generic device tests.

### M-6 First-time related-record saves are not transactional

- Dimensions: error handling, data integrity
- Status: fixed
- Fix applied: first-time scan, match, and verification saves now use the same
  grouped multi-file rewrite transaction as replacements, so retry cannot retain
  or duplicate orphan child records.
- Files:
  - `src/native_storage/native_session_store.cpp:423`
  - `src/native_storage/native_session_store.cpp:528`
  - `src/native_storage/native_session_store.cpp:675`
- Problem:
  - New scan, match, and verification saves append child records first and the
    parent record last.
- Impact:
  - Failure before the parent append leaves hidden orphan children. Retrying the
    same ID appends another child set, which later loads together as duplicates.
- Suggested fix:
  - Use the multi-file transaction path for first saves as well as replacements,
    after H-1 supplies a crash-consistent transaction decision.

### M-7 Serial evidence and some history/audit persistence failures are swallowed

- Dimensions: error handling, observability
- Status: fixed
- Fix applied: persistence results now propagate to one sticky degraded-storage
  state; raw evidence, history, audit, and Modbus save failures are checked, and
  shutdown failures receive one ownerless modal warning even after window teardown.
- Files:
  - `src/win32/main_window_storage.cpp:65`
  - `src/win32/main_window_send.cpp:103`
  - `src/win32/main_window_serial_io.cpp:162`
  - `src/win32/main_window_modbus.cpp:224`
  - `src/win32/main_window_messages.cpp:141`
- Problem:
  - Raw-event persistence is wrapped in a `void` helper or its boolean result is
    ignored. Manual send history, dangerous-operation audit, Modbus raw batches,
    and final shutdown scan persistence have similar unchecked paths.
- Impact:
  - Disk-full, permission, or compaction failures can leave the UI showing normal
    communication while required evidence was not saved.
- Suggested fix:
  - Propagate failures into a visible degraded-storage state, rate-limit repeated
    warnings, and treat dangerous-operation audit failure as an explicit warning.

### M-8 MinGW package name can escape the package root before `rm -rf`

- Dimensions: security, error handling
- Status: fixed
- Fix applied: package names now pass one strict basename validator before any
  build or cleanup operation; `..`, separators, leading/trailing punctuation,
  and other invalid forms are rejected, the package root is physically
  normalized, and cleanup uses option termination.
- Files:
  - `scripts/package-windows-native-mingw.sh:12`
  - `scripts/package-windows-native-mingw.sh:104`
  - `scripts/package-windows-native-mingw.sh:109`
- Problem:
  - `SVM_MINGW_PACKAGE_NAME` is appended to the package root without basename or
    containment validation and is then passed to `rm -rf`.
- Impact:
  - A malformed local/CI environment value such as `../../..` can delete a
    directory outside the artifact root with the build user's permissions.
- Suggested fix:
  - Allow only a strict basename, reject separators and `..`, canonicalize the
    stage path, and verify containment before deletion.

### M-9 Standalone PowerShell packaging can package a stale executable

- Dimensions: error handling, release correctness
- Status: fixed
- Fix applied: the script resolves and invokes the selected CMake executable,
  checks `$LASTEXITCODE`, throws on build failure, and packages only the exact
  supported configuration output after verifying it is a file. The stale
  build-root fallback was removed rather than retained for compatibility.
- Files:
  - `scripts/package-windows-native.ps1:58`
  - `scripts/package-windows-native.ps1:67`
- Problem:
  - The native `cmake --build` exit code is not checked. If an old executable
    remains, the script can continue and package it after the build failed.
- Impact:
  - The hosted workflow currently builds in a separate prior step and invokes the
    packager with `-SkipBuild`, so that path is mitigated. Standalone release use
    is not fail-closed.
- Suggested fix:
  - Capture `$LASTEXITCODE`, throw on non-zero, and bind the selected executable
    to the current build using timestamp or hash evidence.

### M-10 Windows UI capture can reuse a self-test log from another executable

- Dimensions: error handling, test evidence
- Status: fixed
- Fix applied: every capture removes stale self-test evidence, runs the current
  executable, requires an exact standalone `ok` line, records
  `source=current-run`, and includes self-test in both the gate and declared
  required scenarios. The duplicate workflow self-test step was removed.
- Files:
  - `scripts/capture-windows-native-ui.ps1:42`
  - `scripts/capture-windows-native-ui.ps1:124`
- Problem:
  - `self-test.log` is always preserved. Any non-empty prior log skips execution
    of the current executable and is recorded as `preexisting-log`.
- Impact:
  - Reusing a local output directory can create a passing summary for a binary
    that never ran its self-test. Fresh hosted runners and the current workflow
    ordering reduce hosted exposure.
- Suggested fix:
  - Rerun by default. If reuse is required, make it an explicit switch and bind
    the log to executable SHA, commit, timestamp, and terminal status.

### M-11 Hosted package evidence overclaims PTY fault coverage

- Dimensions: consistency, test truthfulness
- Status: fixed
- Fix applied: hosted evidence now claims `synthetic-faults`; an unavailable
  hardware loopback returns exit code 77 and CTest records an explicit skip, and
  documentation plus the consistency checker enforce those semantics.
- Files:
  - `.github/workflows/windows-native-package.yml:58`
  - `.github/workflows/windows-native-package.yml:238`
  - `tests/native_win32_serial_loopback_tests.cpp:374`
  - `scripts/check-docs-artifact-consistency.py:63`
- Problem:
  - The hosted backend summary asserts `pty-faults`, while the same workflow says
    no PTY scenario runs. The loopback test returns success with a skip message
    when no port is configured, and the docs checker requires the overclaim.
- Impact:
  - Consumers can read a CI artifact as proof of PTY fault execution even though
    the actual PTY matrix is separate local-only evidence.
- Suggested fix:
  - Rename hosted coverage to `synthetic-faults` or `fault-models`; reserve PTY
    coverage for a verified summary tied to the same commit and executable.

### M-12 Command-sequence Modbus status uses text and the first retry attempt

- Dimensions: logic, consistency
- Status: fixed
- Fix applied: core scan execution now exposes `Cancelled` as a typed terminal
  status. Failed exchanges are fully recorded before cancellation settlement,
  successful completed exchanges are not overwritten by late cancellation, the
  Win32 worker uses the typed status as its sole cancellation source, and command
  sequence timeout classification uses the final recorded attempt rather than
  error text or the first retry.
- Files:
  - `src/core/modbus_scan_executor_core.h:41`
  - `src/core/modbus_scan_executor_core.cpp:281`
  - `src/command_sequence/command_sequence.cpp:374`
  - `src/win32/native_modbus_scan_worker.cpp:278`
  - `tests/command_sequence_tests.cpp:370`
  - `tests/native_protocol_modbus_tests.cpp:281`
- Problem:
  - Cancellation depends on exact English error text.
  - Timeout classification reads the first attempt instead of the final attempt.
- Impact:
  - `Timeout -> TransportError` is reported as timeout, while
    `TransportError -> Timeout` is reported as failed, contradicting the final
    failure cause. The library currently has no production UI caller.
- Suggested fix:
  - Add a typed cancellation/final-cause field to scan execution and classify from
    the final attempt. Add mixed-retry regression tests.

### M-13 Command delay can silently do no delay and cannot observe mid-delay cancellation

- Dimensions: logic, concurrency
- Status: fixed
- Fix applied: zero delay remains an explicit no-op, while positive delay now
  fails closed without a backend and runs in bounded 100 ms slices with checks
  before each slice and after the final slice. Step, sequence, and evidence all
  publish `Cancelled` when cancellation arrives during the delay.
- Files:
  - `src/command_sequence/command_sequence.cpp:292`
  - `tests/command_sequence_tests.cpp:451`
- Problem:
  - A positive delay reports completion when `sleepForMs` is absent.
  - With a backend, one long sleep cannot poll cancellation; a final delay can
    return the whole sequence as completed after cancellation was requested.
- Impact:
  - The current library has no production caller, so exposure is potential, but
    the API's timing and cancellation contract is unreliable.
- Suggested fix:
  - Reject a missing delay backend or provide a default monotonic delay, and sleep
    in bounded cancellable slices.

## Low Findings

### L-1 UI evidence is not generated automatically for the exact main-branch SHA

- Status: fixed
- Fix applied: UI capture now runs for every `main` push, records
  `GitHubSha`, `GitHubHeadSha`, and `CheckedOutSha`, validates each as a full
  commit SHA, and asserts exact evidence lines before artifact upload. Main runs
  require all three values to match; PR runs bind the tested merge commit while
  recording the source head separately.
- Files:
  - `.github/workflows/windows-native-ui-capture.yml:3`
  - `.github/workflows/windows-native-package.yml:3`
- Problem: package CI runs on `main` pushes, while UI capture is PR/manual only.
- Suggested fix: add a path-filtered `main` push trigger or dispatch and verify the
  completed UI run's `head_sha` before release.

### L-2 Failed Wine UI capture can leave a previous passing summary

- Status: fixed
- Fix applied: startup now removes the previous summary before any prerequisite
  check, and one outer `EXIT` finalizer preserves the original failure code,
  records `FAIL`, and rewrites the current run summary on every exit path.
- Files:
  - `scripts/capture-windows-native-ui-wine.sh:45`
  - `scripts/capture-windows-native-ui-wine.sh:546`
- Problem: startup cleanup omits `ui-evidence-summary.txt`, and the summary is
  rewritten only at successful completion.
- Suggested fix: remove it at startup and use an exit trap to write the current
  run's failed or passed summary.

### L-3 Most C++ tests and Windows jobs have no explicit timeout

- Status: fixed
- Fix applied: all registered CTest cases receive a 30-second default, boundary
  tests use 10 seconds, serial/concurrency tests use 60 seconds, and Win32 serial
  loopback uses 120 seconds while preserving skip code 77. Both Windows jobs use
  a 20-minute job timeout.
- Files:
  - `CMakeLists.txt:215`
  - `.github/workflows/windows-native-package.yml:18`
  - `.github/workflows/windows-native-ui-capture.yml:24`
- Problem: only two Python boundary tests have a CTest timeout; worker/concurrency
  tests and both jobs can hang until platform defaults.
- Suggested fix: set finite test timeouts, with a larger class for serial tests,
  and add workflow `timeout-minutes`.

### L-4 Package reinspection is fragile across checkout EOL and ZIP extractors

- Status: fixed
- Fix applied: Python/MinGW reinspection compares strict UTF-8 `.md`/`.txt`
  content after newline normalization while retaining exact SHA256 comparison
  for other files. Surrogate paths are escaped at summary/stderr output
  boundaries so malformed extractor names produce a readable failed gate rather
  than a traceback. A registered Python CTest creates and extracts real ZIPs
  with Chinese paths, CRLF content, semantic mutations, and POSIX bad-byte names.
- Files:
  - `.gitattributes:10`
  - `scripts/inspect-windows-package.py:262`
  - `scripts/inspect-windows-package.py:529`
  - `tests/version_metadata_tests.cpp:134`
- Problem: documentation uses platform-native EOL but is compared byte-for-byte;
  some extractors expose undecodable Chinese names that can also break UTF-8
  summary writing. Tests check source strings, not a real cross-platform archive.
- Suggested fix: define the inspector's supported platform, or normalize known
  text and add a real ZIP fixture with Chinese names and cross-platform execution.

### L-5 The PTY `stale` scenario does not inject a late old response/completion

- Status: fixed
- Fix applied: the scenario and its timing controls were directly renamed to
  `reopen-generation-isolation` with no compatibility alias. Python, C++, the
  package workflow, active docs, and consistency gate now state that PTY proves
  close/reopen generation isolation; deterministic session/queue tests retain
  responsibility for true stale-completion rejection.
- Files:
  - `scripts/run-windows-native-serial-pty-loopback.py:363`
  - `scripts/run-windows-native-serial-pty-loopback.py:701`
  - `tests/native_win32_serial_loopback_tests.cpp:585`
- Problem: the scenario finishes the old exchange, closes, waits, reopens, and
  performs a new exchange. Exact stale completion rejection remains synthetic.
- Suggested fix: inject a delayed old response after reopen, or rename the PTY
  scenario to `reopen-generation-isolation` and keep stale-completion claims on
  the deterministic unit test only.

### L-6 Workflow plan documents still say pending after 18/18 completion

- Status: fixed
- Fix applied: the project, four phase plans, and eighteen task plans are marked
  `Completed`; all 137 recorded acceptance items are checked. PTY plan text was
  corrected without rewriting the historical completion artifact bound to
  implementation commit `2f69480`.
- Files:
  - `.workflow/serial-transport-layer-v2/project-plan.md:4`
  - `.workflow/serial-transport-layer-v2/state.json:154`
- Problem: 23 plan/task status markers remain `Ready for Approval`, `Pending`, or
  `Planned`, and 137 checklist items remain unchecked while state says completed.
- Suggested fix: mechanically close the project/phase/task plans and their
  verification checklists from recorded evidence; do not change scope or claims.

## Verified Strengths

- `SerialWriteQueue` enforces count and byte limits including active work.
- Request IDs fail closed at exhaustion and remain monotonic in reviewed paths.
- Queued writes use typed terminal results and exactly-once active finalization.
- Generation checks prevent stale results from becoming a valid replacement
  session request; no automatic request replay was found.
- `Win32SerialSession` remains the only COM `HANDLE` owner and does not expose the
  handle publicly.
- The broad `SerialTransport`/`Win32SerialPort` compatibility facade is absent.
- No Qt, Boost.Asio, WIL, SQLite, TCP, or new runtime dependency was introduced.
- Default metadata-only transport evidence remains payload-free.

## Verification Evidence

- Post-fix host build and CTest passed `29/29`.
- Post-fix MinGW application and test targets built successfully.
- Post-fix full MinGW/Wine CTest completed all 36 registered tests with 35 passed,
  one explicitly skipped hardware loopback, and zero failures; after the final
  M-4 decision refinement, its affected Wine test passed again (`1/1`).
- Post-fix Wine tests passed for the changed RTU adapter and serial-I/O decision
  paths (`2/2`).
- M-12/M-13 focused host and MinGW/Wine tests passed (`2/2` on each platform),
  including both mixed retry orders, typed cancellation, preserved attempt and
  observation evidence, missing delay backend, zero delay, mid-delay and final
  slice cancellation, and exact `100 + 100 + 50 ms` completion.
- Windows native self-test reported `ok`; UI performance reported `ui-perf ok`
  after the final refinements.
- Independent final re-reviews found and closed two boundary gaps in unknown RTU
  framing and faulted-batch write publication; the final pass found no remaining
  medium-or-higher regression in the approved scope.
- Focused host tests passed for transport contracts, queue, RTU adapter, serial
  state, Win32 session, command sequence, and directly reviewed caller helpers.
- Boundary checks, documentation checker and negative smoke, shell syntax, Python
  syntax/AST, and Release assertion enablement checks passed.
- The MinGW package-name negative matrix rejected all seven invalid values and
  preserved the external sentinel; a positive package run produced and
  re-inspected the expected archive.
- Independent release-path re-reviews found no remaining medium-or-higher issue
  in M-8 through M-11. PowerShell is unavailable in the local Linux environment,
  so the M-9 native failure path was verified statically and remains covered by
  the Windows workflow rather than claimed as a local runtime result.
- Three independent M-12/M-13 re-reviews found and closed two intermediate
  cancellation-settlement inconsistencies, then reported no remaining
  medium-or-higher issue in the final implementation.
- Generated CTest metadata confirmed the timeout policy for all 29 host tests and
  all 36 MinGW/Win32 tests, including loopback timeout 120 and skip code 77.
- The post-timeout host suite passed `29/29`; the full MinGW/Wine suite passed 35,
  explicitly skipped one hardware loopback test, and failed zero out of 36.
- Wine UI capture overwrote a pre-seeded stale passing summary with a current
  failed summary while preserving missing-executable exit code 3. A full capture
  in a clean Wine prefix then exited zero with exactly one `GateStatus=passed`
  line and no `FAIL` status.
- Static workflow gates confirmed every `main` push trigger, exact SHA provenance
  lines, and 20-minute job timeouts. Three independent L-1 through L-3 re-reviews
  found no medium-or-higher issue.
- The registered package-inspector test created and extracted real Chinese-path
  ZIP fixtures, accepted CRLF/LF-only differences, rejected semantic changes and
  invalid UTF-8, and produced a strict UTF-8 failed summary for a POSIX bad-byte
  filename without a traceback.
- Final host CTest passed `30/30`. Final MinGW/Wine CTest completed all 37
  registered tests with 36 passed, one explicitly skipped hardware loopback,
  and zero failures; all retained the expected timeout classes.
- The renamed seven-scenario PTY matrix passed
  `normal,reopen,timeout,cancel,stress,close,reopen-generation-isolation` with
  5006 completed exchanges. The removed `stale` alias is rejected.
- A final MinGW package run produced and re-inspected all 15 files with package
  documentation links/file-set checks and the overall gate all passed.
- Workflow records now contain 23 `Completed` status markers, 137 checked items,
  and zero approval/pending/planned status markers or unchecked items.
- Independent L-4 through L-6 re-reviews found and closed one historical-evidence
  identity mismatch and several low consistency gaps; the final passes found no
  remaining review issue.
- Previously recorded final implementation CI remains green:
  - package run `29914919999`
  - UI run `29914927486`
  - closure package run `30154921575`

Residual limits in the fixed scope are multi-writer store coordination and
power-loss durability below filesystem rename semantics, the POSIX
check-to-directory-rename race, and hardware-specific stuck-driver behavior that
cannot be deterministically injected in the current suite. GitHub workflow
expressions and PowerShell steps also require the first changed `main`/PR run for
dynamic confirmation because `actionlint` and PowerShell are unavailable in the
local Linux environment. These are environmental verification limits, not open
findings from this review.

## Recommended Fix Order

1. Completed: protect user data with H-1, H-2, M-6, and M-7.
2. Completed: correct the serial foundation with M-1 through M-5.
3. Completed: make release evidence fail closed with M-8 through M-11.
4. Completed: close the original typed-result and cancellation contract with
   M-12 and M-13.
5. Completed: bind UI evidence, fail closed on Wine capture, and bound test/jobs
   with L-1 through L-3.
6. Completed: make package reinspection portable, name PTY evidence accurately,
   and close workflow records with L-4 through L-6.

The approved fix passes changed product, regression tests, and release tooling
only for the 21 approved findings. No unrelated product feature or compatibility
layer was added.

## Requirements Alignment

The work remains aligned with the approved product direction: a Win32/C++20,
serial-only foundation with one session owner, bounded writes, typed terminal
results, generation isolation, and protocol logic above the byte transport. It
does not restore Qt, add TCP/UDP, introduce a broad compatibility facade, or add
a runtime dependency. Variable-bit layouts and codecs such as Gray code remain
deliberately above transport and outside this stabilization pass.

M-12 and M-13 closed the original typed-error, cancellation, and timing gaps;
L-4 through L-6 closed release portability and record accuracy without adding a
new product feature surface.
