# DeepWiki Phase Research - Phase 3: Production Hardening

Generated: 2026-07-16T02:09:55+08:00

## Dependency Inventory

| Dependency | GitHub Repository | Phase Usage | Research Status |
|---|---|---|---|
| Windows API | `microsoft/Windows-classic-samples` | I/O completion, cancellation, synchronization, native evidence, and resource lifetime | `structure` and broad `ask` completed |
| CMake/CTest | `Kitware/CMake` | Focused and full test registration, filtering, serialization, and failure reporting | `structure` and broad `ask` completed |
| Wine | `wine-mirror/wine` | Win32 execution, COM mapping, PTY validation, self-test, and UI-performance evidence | `structure` and broad `ask` completed |
| GitHub Actions runner | `actions/runner` | Windows job execution, exit-code propagation, summaries, and artifacts | `structure` and broad `ask` completed |

No new runtime dependency is introduced. C++20 standard-library containers and
checked arithmetic remain local implementation details and require no external
repository query.

## Queries Performed

For each repository, the documentation structure was queried first. One broad
question then asked for APIs and practices relevant to Phase 3. A final
cross-repository query covered MinGW/Wine/CTest/Actions integration.

DeepWiki returned useful concrete CMake/CTest guidance and supporting Win32
patterns. Its Wine, Actions, and cross-repository answers correctly noted that
the repositories do not define this project's PTY, package, or evidence gates.
Those answers are therefore supporting context only; the checked-in project
scripts and tests remain the behavioral source of truth.

## Applicable Findings

### Windows API

- Per-handle and per-operation context must remain owned until the operation
  reaches a terminal state. A cancellation request is not completion evidence.
- `CancelSynchronousIo` is thread-targeted. It cannot be treated as an
  interchangeable implementation of `CancelIoEx` or as proof that buffers and
  handles are already reusable.
- Worker stop, terminal accounting, thread settlement, event release, and COM
  handle release must remain ordered under one session owner.
- `GetLastError` values must be captured at the failing API boundary. Localized
  text is presentation data, not a queue or reconnect decision input.
- The samples provide generic I/O and synchronization patterns, not a complete
  synchronous COM-port contract. Driver-specific cancellation and unplug
  behavior require project tests and honest evidence labels.

### CMake and CTest

- Tests should be registered explicitly with `add_test(NAME ... COMMAND ...)`.
- `ctest -R` is appropriate for focused task verification; a release gate must
  also run unfiltered CTest so filtering cannot hide unrelated regressions.
- `--output-on-failure` plus the CTest exit code is the primary CI failure
  signal. Logs are supporting evidence and must not replace exit-code checks.
- `RUN_SERIAL`, labels, timeouts, and deterministic names are available when
  tests share a PTY, Wine prefix, or other exclusive fixture.
- Host and MinGW builds must be treated as separate configured trees. A green
  host test does not establish that the Win32 session target compiled or ran.

### Wine

- Wine maps Windows serial behavior onto Unix facilities, but its repository
  does not define this project's PTY matrix or release classification.
- Each evidence run should use an isolated prefix and deterministic COM mapping;
  stale prefixes or shared PTYs can create false positives and cross-test state.
- A Wine result proves the adapter/test contract under Wine. It does not prove
  identical behavior for every physical USB-serial driver.
- Self-test, UI performance, and PTY commands must be checked independently and
  must propagate non-zero exit codes to the caller.

### GitHub Actions Runner

- The runner executes the repository's workflow; it does not supply project-
  specific CMake, serial, package, or evidence policy.
- PowerShell and shell steps must fail on command errors rather than relying on
  later artifact inspection to discover a failed gate.
- Step summaries improve diagnosability but are not pass/fail authority.
- Required artifacts must be asserted before upload. Upload success alone does
  not prove that every expected evidence file exists or contains a success
  result.

## Cross-Repository Integration Boundaries

- The cross-repository query could not inspect this project's implementation.
  Consequently, project-local `CMakeLists.txt`, workflow files, PTY scripts,
  package scripts, and tests override generic repository patterns.
- Local and CI commands must use the same target names, scenarios, evidence
  vocabulary, and non-zero failure semantics.
- Queue accounting is platform-neutral. Win32/Wine APIs should not be pulled
  into the queue simply to satisfy integration testing.
- Metadata evidence must not contain payload bytes by default. Existing raw
  serial evidence remains an explicit, separate feature.

## Guidance by Task

### Task 1 - Integrate Queue Backpressure

- Keep count and byte budgets in the neutral queue policy and include the active
  reservation until terminal release.
- Expose immutable queue evidence to the UI; do not duplicate limits or compute
  ownership-sensitive counters in presentation code.
- Use focused host and MinGW tests before the full trees. PTY and Wine evidence
  confirm integration but do not replace deterministic queue tests.

### Task 2 - Guarantee Exactly-Once Completion

- Separate cancellation request, native settlement, and terminal publication.
- Preserve request ID, generation, deadline, native code, and partial byte count
  through the one terminal path.
- Do not release operation storage or native resources before worker settlement.

### Task 3 - Wire Typed Errors and Evidence

- Keep stable status/category/native code as control-flow evidence.
- Localize only at the UI boundary and log metadata without payload bytes by
  default.

### Task 4 - Expand PTY Fault Matrix

- Isolate Wine prefixes and PTYs, keep scenario names deterministic, and label
  the result as local adapter evidence rather than physical-driver proof.
- Ensure every scenario failure propagates through the script exit code and
  summary gate status.

### Task 5 - Strengthen CI Release Gates

- Run the complete configured tree, preserve explicit focused evidence, and
  assert every required artifact before upload.
- Keep summaries descriptive; command exit status and explicit content checks
  remain authoritative.

## Phase-Level Confidence

Confidence is high for the queue/accounting and CTest design, medium-high for
single-owner Win32 settlement patterns, and medium for Wine/physical-driver
equivalence. DeepWiki completed without WebSearch fallback, but several answers
explicitly lacked project-specific context; those limitations are retained
rather than filled with assumptions.
