# Phase 4 Task 03 API Reference

Generated: 2026-07-20

## Research Record

This task-level research was performed against the dependency table in
`task-03-run-final-release-verification.md`.

DeepWiki was intermittently unavailable during the phase and the first
sandboxed Task 03 call failed with curl exit code 6. After network escalation,
focused `ask` calls returned for all three task dependencies. No further network
queries were made after the interruption. The returned guidance was
cross-checked against the protocol-approved fallback sources:

- `.workflow/serial-transport-layer-v2/deepwiki-cache/phase-4-research.md`;
- `.workflow/serial-transport-layer-v2/deepwiki-cache/phase-3-task-05-api-reference.md`;
- the checked-in CMake graph, shell/Python scripts, and GitHub workflows;
- local tool help for CMake/CTest and GitHub CLI.

| Repository | Focused API/evidence question | Result |
|---|---|---|
| `Kitware/CMake` | `cmake -S/-B`, `cmake --build --parallel`, `ctest --test-dir --output-on-failure --no-tests=error`, clean-tree and exit-code evidence | Live answer returned after the initial network failure; consistent with the phase cache and local CMake 3.25.1 behavior. |
| `wine-mirror/wine` | Wine COM mapping, native executable exit propagation, and the boundary between PTY evidence and physical Windows hardware evidence | Live answer returned; consistent with the Wine man-page behavior already cached and the current PTY harness. |
| `actions/runner` | step exit propagation, `continue-on-error`, pipeline masking, run/log/artifact evidence boundaries | Live answer returned; consistent with the phase cache and the checked-in PowerShell workflows. |

Local environment observed during research:

- CMake/CTest: `3.25.1`;
- Wine: `wine-8.0 (Debian 8.0~repack-4)`;
- project version source: `cmake/svm_version.cmake`, version `1.0.4`;
- native package artifact: `SerialValueMatcherNative-win32-native-x64`;
- local MinGW artifact: `SerialValueMatcherNative-win32-native-x64-mingw`;
- UI artifact: `windows-native-ui-screenshots`.

No new dependency, compatibility layer, TCP/UDP implementation, Qt runtime, or
codec implementation is justified by this research.

## Evidence Inventory Contract

The completion report should identify the exact source tree before listing test
results. Record at least:

- `git rev-parse HEAD` and `git status --short`;
- the diff identity being verified, including any intentionally uncommitted
  Task 03 report/state files;
- operating system, architecture, generator, compiler, CMake, CTest, Ninja,
  MinGW, Wine, Python, and GitHub CLI versions;
- command text, start/end timestamps, elapsed duration, numeric exit code,
  pass/fail result, and durable log/evidence path for every mandatory gate;
- the Wine prefix and COM mapping used by PTY/package checks;
- final CI workflow run IDs, URLs, head SHAs, conclusions, and artifact names;
- limitations and any optional hardware/UI work that was not run.

A green-looking line is not enough. A mandatory item passes only when the
command exits zero and its machine-readable evidence contains the required
markers. Preserve raw logs; summarize them rather than replacing them.

## CMake And CTest Contract

### Fresh configuration

`cmake -S <source> -B <build>` selects the source and build trees and creates an
out-of-source build directory when needed. A unique empty directory under
`/tmp` is the strongest evidence because it cannot reuse an old cache, object,
or test registration. The Task 03 host tree should therefore use the planned
`/tmp/svm-transport-v2-final` path and `SVM_BUILD_WIN32_APP=OFF`.

Do not treat deletion/reconfiguration of an in-repository build directory as
the fresh host gate. The existing MinGW tree is intentionally reused by the
project helper and must be identified separately in the report.

### Complete build

`cmake --build <build> --parallel [N]` returns nonzero when the underlying build
fails. The host build may use available parallelism. The final MinGW build must
use `--parallel 1` as planned so every cross-test executable is built before
cross CTest runs.

`scripts/build-windows-native-mingw.sh` configures the cross tree but builds only
the `svm-native-win32` target. It is not the complete MinGW build gate. Follow it
with:

```text
cmake --build build-windows-native-mingw --parallel 1
```

### Test execution

`ctest --test-dir <build>` selects the configured test tree.
`--output-on-failure` supplies diagnostics. `--no-tests=error` makes an empty or
mis-selected test set fail instead of appearing green. A zero result means the
selected tests passed; a nonzero result is blocking.

Focused runs are supplementary. The final host and MinGW gates must be
unfiltered and must include `--no-tests=error`:

```text
ctest --test-dir /tmp/svm-transport-v2-final --output-on-failure --no-tests=error
ctest --test-dir build-windows-native-mingw --output-on-failure --no-tests=error
```

The current focused transport set should use exact registered names and reject
zero tests. It includes the neutral boundary/session/queue/RTU/command tests,
plus the Win32 tests in the MinGW tree:

- `transport_boundary_tests`;
- `transport_boundary_self_tests`;
- `serial_write_queue_tests`;
- `serial_session_contract_tests`;
- `native_modbus_transport_adapter_tests`;
- `native_protocol_modbus_tests`;
- `command_sequence_tests`;
- `native_serial_io_state_tests`;
- `native_reconnect_state_tests`;
- `native_win32_serial_tests`;
- `native_win32_serial_loopback_tests` where registered.

Do not hard-code the total test count in the completion contract. Task 02 added
two boundary tests, and future narrow tests may legitimately change the count.
Record the observed count and require `100% tests passed` plus exit code zero.

## Wine And PTY Contract

### COM mapping

Wine maps Windows COM names through lowercase links in
`$WINEPREFIX/dosdevices`, for example:

```text
$WINEPREFIX/dosdevices/com5 -> /dev/pts/N
```

The current Python harness creates one PTY per scenario, replaces the mapping
only for the bounded child run, keeps the PTY master open, and restores/removes
the temporary mapping. Record `WinePrefix`, `ComPort`, `Pty`,
`PtyInstanceCount`, `UniquePtyCount`, and `PtyIsolation` from the generated
summary. If `/root/.wine` is reused, describe it as a reused approved prefix,
not clean-prefix evidence.

### Exit codes and strict execution

Wine exposes the Windows executable result through the `wine` process. Required
shell gates must preserve that result and accept only zero. Keep
`set -euo pipefail`; do not pipe a required native command through a consumer
that can hide the original status.

The package script is fail-closed by default:

- `SVM_STRICT_WINE_TEST=1` by default;
- unavailable Wine/Xvfb or a failed `--self-test`/`--ui-perf-test` exits `4`;
- a strict pass appends `Wine gate status: passed` and
  `Wine gate strict: 1` to the package summary;
- `failed-soft`, `unavailable`, and `skipped-by-request` are diagnostic states,
  never release passes.

Use the already approved absolute prefix for the strict final package run and
record it explicitly:

```text
env SVM_WINEPREFIX=/root/.wine bash scripts/package-windows-native-mingw.sh
```

### Authoritative PTY matrix

The Task 03 plan's example still lists five scenarios. The current harness,
Phase 4 cache, CI boundary note, and documentation define seven. The final
matrix must therefore be:

```text
normal,reopen,timeout,cancel,stress,close,stale
```

Run with an explicit summary destination. A pass requires both process exit
zero and all of these assertions:

- stdout: `python serial matrix summary gate-status=passed`;
- summary: `GateStatus=passed`;
- summary: `Classification=local-only-release-candidate-evidence`;
- summary: `Scenarios=normal,reopen,timeout,cancel,stress,close,stale`;
- summary: `Transport=serial-adapter-contract`;
- summary: `CiExecutesPtyMatrix=no`.

Do not substitute the Windows workflow's
`GateStatus=documented-local-only`. That marker truthfully means the hosted
Windows job documents the local PTY requirement but did not execute it.

### Evidence limit

Wine translates Win32 serial APIs to Unix serial behavior. A Wine/PTY pass is
local release-candidate evidence for the application and serial-adapter
contract. It does not prove:

- a real Windows vendor driver;
- PLC interoperability;
- USB-serial converter behavior;
- electrical noise, framing/parity/overrun faults, or hardware flow control;
- unplug/replug timing or every physical cancellation path.

Retain the current one-second cancellation claim only for the deterministic
native/Wine environments actually covered. Real PLC/USB-serial smoke testing
remains optional hardware coverage and must be listed separately.

## Package Verification Contract

The strict package command must complete the Wine gate, create a clean staging
directory and archive, compute the ZIP SHA256 sidecar, and run
`scripts/inspect-windows-package.py`.

The inspector is the authority for structured package acceptance. Its current
mandatory checks include:

- ZIP size at most `5242880` bytes;
- extracted size at most `8388608` bytes;
- `svm-native-win32.exe` and every required documentation file present;
- ZIP SHA256 matches the `.sha256.txt` sidecar;
- PE import inspection is available and contains no forbidden import;
- no Qt/SQLite/.NET runtime file and no unexpected DLL file;
- required Chinese UTF-16LE text probes are present;
- packaged documentation links and the packaged documentation file set match
  the repository;
- VERSIONINFO and artifact names match `cmake/svm_version.cmake`;
- final `Gate status: passed`.

Record the ZIP, SHA256 sidecar, package summary, staged file set, imported DLL
list, largest files, exact size values, Wine gate fields, and command exit code.
Artifact upload digest is not a replacement for the ZIP SHA256 sidecar.

## Local UI Evidence Contract

Task 03 Step 6 is conditional. Determine whether the verified implementation
range changed UI implementation/capture paths. If no UI path changed, record
`not required` with the compared commit range and changed-file list; do not
manufacture a local screenshot requirement for transport-only work.

The strict package run still executes the native self-test and UI-performance
gate. The final hosted Windows UI workflow remains mandatory CI evidence because
it also validates the complete Windows tree and current artifact contract.

If local UI capture is required, use
`scripts/capture-windows-native-ui-wine.sh` and require nonempty screenshots,
`capture-status.txt`, `self-test.log`, `ui-perf-test.log`, `window-info.txt`, and
`ui-evidence-summary.txt`; require `GateStatus=passed`. Screenshots are visual
evidence only and must never be presented as serial transport correctness.

## Static Repository Gates

Run and record numeric exit codes for:

```text
python3 scripts/check-transport-boundaries.py --self-test
python3 scripts/check-transport-boundaries.py
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

The transport boundary checker is the authoritative fail-closed check for Qt,
Win32, UI, matcher/analysis/codec, storage, and legacy facade dependencies under
`src/transport`.

Supplement it with narrow source searches for actual definitions/includes,
not broad prose matches. Verify no new `SerialTransport`, `Win32SerialPort`, Qt
include/runtime, socket implementation, TCP/UDP implementation, or general Gray
codec appears in the changed transport scope. Existing fixed `Gray16` analysis
in the upper-layer `analysis_core` is pre-existing and allowed; the required
claim is that no Gray decoding or variable bit-layout codec leaked into
`src/transport` or was added by this workflow.

Review `git status --short` as well as `git diff --check`: the latter does not
validate unrelated untracked files. Preserve unrelated workflow directories and
do not stage them in the Task 03 commit.

## GitHub Actions Runner Contract

### Failure propagation

A required shell/native command's nonzero exit makes a step fail unless workflow
policy softens it. `continue-on-error: true` changes the step conclusion policy
and must not be used for release gates.

Shell pipelines can mask an earlier command unless `pipefail` or explicit status
capture is used. In PowerShell, when native output is captured or followed by
another native command, save `$LASTEXITCODE` immediately and exit with it after
writing the log. The current package workflow already does this for CTest.

`GITHUB_STEP_SUMMARY` is descriptive. Writing a success sentence cannot turn a
failed child process into a successful gate.

### Dispatch and identity

`gh workflow run <workflow> --ref <ref>` creates a `workflow_dispatch` event,
but it runs the remote ref, not local unpushed or dirty files. Before accepting
CI evidence, require the run's `headSha` to equal the committed implementation
tree intended for release.

After dispatch, do not assume the newest repository run belongs to this task.
Query by workflow/ref/time, then record and inspect the selected run ID. Useful
GitHub CLI evidence operations are:

```text
gh workflow run windows-native-package.yml --ref <remote-ref>
gh workflow run windows-native-ui-capture.yml --ref <remote-ref>
gh run list --workflow <workflow> --branch <branch> --json databaseId,status,conclusion,headSha,url,event,createdAt,updatedAt
gh run view <run-id> --exit-status
gh run view <run-id> --log
gh run download <run-id> --name <artifact-name> --dir <evidence-dir>
```

The completion report should record, for both workflows:

- workflow file/name and event;
- run ID and URL;
- head branch/ref and exact `headSha`;
- status, conclusion, created/updated timestamps, and duration;
- job/step conclusions and the relevant CTest/gate log excerpts;
- artifact ID/digest if visible in the summary;
- downloaded artifact name and local evidence directory.

### Artifact completeness

The package workflow must pass with artifact
`SerialValueMatcherNative-win32-native-x64` and its nine explicit evidence
files: ZIP, SHA256 sidecar, package summary, native CTest log, native self-test
log, native UI-performance log, Phase 2 backend regression record, PTY boundary
note, and PTY machine-readable summary.

The UI workflow must pass with artifact `windows-native-ui-screenshots` and its
required screenshots/status/self-test/UI-performance/window-info/summary files.

`if-no-files-found: error` and upload success are secondary guards. The checked-
in workflows' pre-upload nonempty-file and required-content assertions remain
the completeness authority. The upload artifact digest proves the uploaded
Actions object; it does not replace file-set checks, screenshot/status review,
or the package ZIP SHA256 sidecar.

## Required Gate Order

Use this order in the completion report so later evidence cannot conceal an
earlier failure:

1. Record source/diff identity and environment versions.
2. Fresh host configure and complete build.
3. Focused host transport tests, then full unfiltered host CTest.
4. MinGW helper configure/app build, then complete MinGW build.
5. Focused MinGW transport tests, then full unfiltered MinGW CTest.
6. Seven-scenario local Wine/PTY matrix with summary assertions.
7. Strict Wine self-test/UI-performance and package inspection.
8. Conditional local UI capture, or a documented not-required decision.
9. Boundary, documentation, forbidden-symbol, diff, and status checks.
10. Final package and UI workflow runs with downloaded artifact inspection.
11. Known limitations and optional physical-hardware coverage.
12. Mark workflow complete only if every mandatory result is zero/passed.

Any mandatory failure leaves Task 03 and workflow state active. Do not write a
completion claim around a failed, skipped, unavailable, soft-failed, empty-test,
wrong-head-SHA, or incomplete-artifact result.

## Main-Agent Handoff

- Use the current seven-scenario PTY matrix, not the stale five-scenario example
  in the Task 03 command block.
- Use `/root/.wine` only with an explicit reused-prefix note.
- Run full unfiltered host and MinGW CTest after focused checks.
- Treat local UI capture as conditional, but treat both final Windows workflows
  and their downloaded artifact contents as mandatory.
- Require CI `headSha` equality with the committed implementation tree.
- Keep physical PLC/USB-serial validation and broad Gray/bit-layout codec work
  outside this transport-v2 completion claim.
