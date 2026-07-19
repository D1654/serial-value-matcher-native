# Phase 3 Task 05 API Reference

Generated: 2026-07-20

## Research Record

Live DeepWiki `ask` calls completed for every dependency row in the Task 05
plan. The queries asked for command parameters, return values, failure
propagation, and CI evidence boundaries rather than general library summaries.

| Repository | Queries | Coverage | Result |
|---|---:|---|---|
| `Kitware/CMake` | 1 | `add_test`, complete versus filtered CTest, `-C Release`, exit codes, `--no-tests=error`, `RUN_SERIAL`, `TIMEOUT` | Concrete and directly applicable. |
| `microsoft/Windows-classic-samples` | 1 | `CreateFileW`, `ReadFile`, `WriteFile`, immediate `GetLastError`, synchronous cancellation, native-test limits | Useful synchronous-worker guidance; it does not justify adding `CancelIoEx` to this backend. |
| `wine-mirror/wine` | 1 | `wine`, `wineboot`, `WINEPREFIX`, Xvfb, cleanup, serial/PTTY evidence limits | Useful for strict local execution and evidence classification; project scripts remain authoritative for COM/PTTY setup. |
| `actions/runner` | 2 | PowerShell `$LASTEXITCODE`, generated shell wrapper, step summaries, failure propagation, artifact boundary | Concrete runner behavior. `actions/upload-artifact` inputs are owned by another repository, so checked-in workflow configuration is the project authority. |
| `ip7z/7zip` | 2 | `7z a -tzip`, exact process exit codes, clean archive creation, stale-entry risk, runtime-dependency boundary | Concrete enough for a strict build-time packaging rule. |
| `python/cpython` | 1 | `pathlib`, `hashlib.sha256`, `argparse`, `sys.exit`, deterministic package inspection | Concrete and consistent with the existing inspector. |

No new build or runtime dependency is justified by this research.

## Confidence And Limits

- CMake/CTest, PowerShell runner, 7-Zip, and Python failure semantics are high
  confidence.
- Wine behavior is high confidence for process/prefix semantics but only medium
  confidence for serial behavior because a POSIX PTY under Wine is not a
  physical Windows serial driver.
- The Windows sample repository provides generic synchronous I/O patterns, not
  a vendor-neutral serial-driver conformance suite.
- The current Task 04 research is authoritative for the synchronous backend:
  this task must not add `CancelIoEx`, advertise overlapped-I/O cancellation,
  or weaken retained-resource settlement behavior merely because the Task 05
  dependency table names that API.

## Exact Dependency Contracts

### CMake And CTest

`add_test(NAME <name> COMMAND <target>)` registers the executable target in the
generated `CTestTestfile.cmake`. A test normally passes only when its command
returns zero. A nonzero child result makes CTest fail unless a test property
explicitly changes the rule.

For the Windows multi-configuration build, the complete release invocation is:

```text
ctest --test-dir <build-dir> --output-on-failure -C Release --no-tests=error
```

The important semantics are:

- `--test-dir` selects the configured build tree.
- `-C Release` selects the Release executable in a Visual Studio build.
- `--output-on-failure` affects diagnostics, not pass/fail policy.
- No `-R` or `-E` belongs in the final release gate. Focused regex runs are
  useful before it, but they cannot replace the unfiltered tree.
- `--no-tests=error` turns a missing or accidentally empty configured test tree
  into an immediate failure. Do not rely only on a later log substring check.
- `RUN_SERIAL` is appropriate only for tests that share an exclusive resource.
  The Task 04 Python harness already serializes a Wine prefix and is not a CTest
  test. Adding `RUN_SERIAL` broadly would slow the suite without adding safety.
- A bounded `TIMEOUT` is useful for an executable that can block on a native
  serial operation. It is not a substitute for the harness's per-child timeout.

The checked-in source graph is already complete:

- `svm_slim_core` contains the neutral serial queue and RTU adapter.
- `svm_win32_serial` includes `win32_serial_session.cpp` and the enumerator when
  `WIN32` is true.
- `native_win32_serial_tests` links the real Win32 serial library.
- `native_win32_serial_loopback_tests` is registered only when `WIN32` is true.
- A clean host configuration registers 27 tests.
- The current full MinGW configuration registers 34 tests, including both
  native Win32 serial targets.

Therefore Task 05 should not invent duplicate executables or compatibility
targets. Change `CMakeLists.txt` only if a real missing source/test is found
during execution, or to add a narrowly justified timeout to the Win32 loopback
test.

### Windows API

The relevant synchronous API result rules remain:

- `CreateFileW` returns `INVALID_HANDLE_VALUE` on failure.
- `ReadFile` and `WriteFile` return zero (`FALSE`) on immediate failure.
- `GetLastError()` must be captured immediately after the failing API call,
  before logging, formatting, cleanup, or another Win32 call can overwrite it.
- A successful synchronous call may still transfer fewer bytes than requested;
  typed byte-count and deadline evidence remains necessary.
- Cancellation request and native settlement are distinct. The worker, handle,
  and payload storage remain owned until the synchronous call returns.

The current backend opens the COM handle without `FILE_FLAG_OVERLAPPED` and
uses the already-reviewed synchronous-worker cancellation path. Task 05 is a
build/release task, not a backend redesign. It must compile and execute the real
session implementation but must not add `CancelIoEx` or claim overlapped I/O.

Native Windows CTest can establish that the implementation compiles, links,
runs, and satisfies deterministic contracts on the hosted environment. It
cannot establish vendor USB-serial cancellation, unplug codes, flow-control
timing, electrical faults, or physical PLC interoperability.

### Wine And Xvfb

- `wine <exe> ...` exposes the Windows process result to the caller; a strict
  shell gate must accept only zero.
- `wineboot` returns zero on successful setup and nonzero on bootstrap/shutdown
  failures. The Task 04 harness performs bounded setup and cleanup. The MinGW
  package script may use an existing prefix, so it must not describe that as a
  clean-prefix PTY run.
- `WINEPREFIX` isolates registry, wineserver state, and DOS devices. Reusing a
  prefix can retain state, so evidence must record the prefix actually used.
- `xvfb-run` must remain in the same checked command as both GUI invocations.
  A failure to start Xvfb or a nonzero child must propagate to the packaging
  script.
- Process cleanup matters after signals and timeouts; stale Wine children can
  interfere with a later run. Task 04 owns the PTY harness cleanup and prefix
  lock.

The existing MinGW package script is already fail-closed by default:

- `set -euo pipefail` is active.
- `SVM_STRICT_WINE_TEST` defaults to `1`.
- strict failure or unavailable `wine`/`xvfb-run` exits before packaging.
- successful evidence appends `Wine gate status: passed` and
  `Wine gate strict: 1` to the package summary.

Task 05 should preserve that behavior. An explicitly requested non-strict or
skip mode may remain available for developer diagnostics, but it must be
machine-visible as `failed-soft`, `unavailable`, or `skipped-by-request`; it
must never be reported as a strict pass.

Wine/PTTY evidence proves the adapter contract under Wine's Unix serial
translation. It is not physical-driver evidence and it is not evidence that a
GitHub `windows-2022` job executed a POSIX PTY.

### GitHub Actions Runner And PowerShell

The default `shell: pwsh` runner wrapper prepends stop-on-PowerShell-error
behavior and appends logic equivalent to:

```powershell
if ((Test-Path -LiteralPath variable:\LASTEXITCODE)) { exit $LASTEXITCODE }
```

That makes a final native command's nonzero result fail the step. However, when
output is captured, transformed, or followed by more native commands, the
original code must be saved immediately:

```powershell
$output = & ctest ... 2>&1
$exitCode = $LASTEXITCODE
$output | Tee-Object -FilePath $log
if ($exitCode -ne 0) { exit $exitCode }
```

The package workflow already follows this pattern for CTest. Preserve it.
Simple self-test/UI steps currently end with the native executable invocation,
so the runner wrapper already propagates their result. Do not add a redundant
compatibility helper merely to restate runner behavior. If later commands are
added after those invocations, capture `$LASTEXITCODE` immediately at that
time.

`GITHUB_STEP_SUMMARY` is descriptive output only. Writing `passed` into it does
not make a failed command pass. Likewise, the upload action's artifact digest
proves the bytes accepted by the upload action; it does not replace the package
ZIP SHA256 sidecar, package inspector, or required-evidence assertions.

The project must assert each required artifact before upload. The existing
package workflow already checks nonempty files for:

- package ZIP;
- SHA256 sidecar;
- package summary;
- complete CTest log;
- self-test log;
- UI-performance log;
- the established `phase-2-backend-regression.txt` closure path;
- PTY boundary note;
- PTY machine-readable summary.

Keep `if-no-files-found: error`, but do not treat it as completeness validation:
it only proves that at least one configured path matched. Explicit per-file and
required-content assertions remain authoritative.

### 7-Zip

`7z a -tzip <archive> <input>` creates or updates a ZIP. The relevant console
exit codes are:

| Code | Meaning | Strict package result |
|---:|---|---|
| 0 | success | accept |
| 1 | warning | reject |
| 2 | fatal error | reject |
| 7 | command-line error | reject |
| 8 | insufficient memory | reject |
| 255 | user interruption | reject |

The existing MinGW script removes the prior ZIP before `7z a`. Preserve that
ordering: updating an existing archive can retain stale entries that disappeared
from the staging directory. `set -e` correctly accepts only exit code zero.

7-Zip is a build-time tool here. The package contains the native executable and
documentation, not `7z.exe` or `7z.dll`; invoking it does not create an
application runtime dependency. The package inspector's unexpected-DLL and
forbidden-runtime checks remain the runtime authority.

### Python Package Inspection

The existing inspector already uses deterministic primitives correctly:

- `Path.exists()`, file type checks, recursive traversal, and relative-path
  sets establish package layout and documentation-set equality.
- `Path.stat().st_size` establishes ZIP and extracted size gates.
- `hashlib.sha256()` with chunked input and a normalized hexadecimal digest
  establishes ZIP and executable integrity.
- missing required files, unexpected DLLs, forbidden imports/runtimes, Unicode
  probe failures, documentation link/set failures, version drift, hash mismatch,
  and size overflow accumulate as failures.
- `main()` returns `1` on a failed gate and `raise SystemExit(main())` propagates
  that result to shell and CI.

Do not replace these structured checks with log-only string matching. Workflow
content assertions should confirm that the inspector ran and passed; the
inspector itself remains responsible for calculating the result.

## Current Repository Audit

### Already correct

- The Windows package workflow builds the complete Release target graph and
  runs an unfiltered CTest tree before self-test, UI performance, packaging,
  docs consistency, and upload.
- CTest output is written to `native-ctest.log`, and its saved exit code is
  propagated before later steps run.
- The package inspector enforces 5 MiB ZIP and 8 MiB extracted limits, SHA256
  integrity, PE imports, forbidden Qt/SQLite/.NET runtimes, unexpected DLLs,
  required files, Unicode probes, documentation links, and documentation-set
  equality.
- Artifact files are individually asserted as nonempty before upload, and
  `if-no-files-found: error` remains enabled.
- The UI workflow trigger already includes `src/transport/**`, `src/win32/**`,
  `tests/**`, `CMakeLists.txt`, and the PTY harness.
- The MinGW package script is strict by default and accepts only a successful
  7-Zip/Python/Wine command chain.
- The package upload summary explicitly says its digest does not replace the
  ZIP SHA256 sidecar.

### Gaps to close in Task 05

1. Add `--no-tests=error` to complete CTest release invocations. The later
   `100% tests passed` assertion is useful evidence, but zero-test failure
   belongs at the CTest command boundary.
2. Keep the established `phase-2-backend-regression.txt` filename, but append
   stable transport-v2 coverage: count/byte backpressure including active work,
   exactly-once terminal completion, typed error/evidence fields, generation
   isolation, close/cancel/timeout/reopen, and stale completion rejection.
3. The Windows workflow still documents Task 04's old five-scenario set. The
   current harness default and valid matrix are
   `normal,reopen,timeout,cancel,stress,close,stale`. Update the boundary note,
   summary, local command, table, and completeness assertion together.
4. Preserve `GateStatus=documented-local-only`,
   `Classification=local-only-release-candidate-evidence`, and
   `CiExecutesPtyMatrix=no`. Do not copy a developer's local
   `GateStatus=passed` result into the Windows CI-generated summary.
5. Strengthen stable summary assertions to include at least
   `Zip sha256 file matches: yes`, `Native exe present: yes`, the fixed size
   ceilings, and the existing final `Gate status: passed`. Do not recompute the
   entire inspector in workflow code.
6. Extend the consistency checker with stable workflow/package terms only:
   full CTest plus `--no-tests=error`, transport-v2 closure wording, the seven
   PTY scenario names, `CiExecutesPtyMatrix=no`, strict Wine summary fields,
   and the existing UI trigger paths. Avoid volatile test counts or localized
   prose fragments.

## Minimal File-Level Recommendation

### `CMakeLists.txt`

- First verify the existing targets rather than adding duplicates; the current
  graph already registers the hardened session and tests.
- If a hang bound is added, scope `TIMEOUT` to
  `native_win32_serial_loopback_tests`. Do not serialize unrelated tests.
- Do not move Win32 source into `svm_slim_core` and do not add `CancelIoEx`.

### `.github/workflows/windows-native-package.yml`

- Add `--no-tests=error` to the unfiltered Release CTest command and displayed
  command evidence.
- Update the retained backend closure file contents with transport-v2 terms.
- Update all PTY boundary text and assertions to the seven Task 04 scenarios.
- Keep CI-generated PTY status `documented-local-only`, not `passed`.
- Preserve the existing per-file assertions and add only stable pass terms from
  the package summary.
- Preserve all artifact names and the upload action configuration.

### `.github/workflows/windows-native-ui-capture.yml`

- Add `--no-tests=error` to its unfiltered Release CTest command.
- Keep the existing transport, Win32, tests, CMake, and PTY-harness path
  triggers. No UI redesign or new screenshot is required by transport v2.
- Preserve current UI evidence completeness and upload rules.

### `scripts/package-windows-native-mingw.sh`

- Preserve `set -euo pipefail`, strict default `1`, the combined Xvfb/Wine
  command, clean ZIP removal, 5/8 MiB inspector arguments, and appended Wine
  status fields.
- If environment values are hardened, accept only explicit `0` or `1`; reject
  malformed values rather than silently treating them as non-strict.
- Do not make a strict Wine failure soft, and do not label explicit skip/non-
  strict output as a pass.

### `scripts/check-docs-artifact-consistency.py`

- Require exact stable release-gate tokens, not implementation-specific line
  numbers or a hard-coded total test count.
- Check that the package workflow keeps all nine required evidence files,
  `if-no-files-found: error`, `CiExecutesPtyMatrix=no`, and the seven-scenario
  local-only boundary.
- Check that the UI workflow retains all transport-related trigger paths.
- Check that the MinGW package script retains strict Wine status terms and the
  existing 5/8 MiB defaults.
- Preserve old-executable, Qt-route, package-name, documentation-link, and
  documentation-set checks.

## Required Gate Order

The release-candidate order should remain:

1. Configure the intended build tree.
2. Build the complete tree, not only `svm-native-win32`.
3. Run unfiltered CTest with output on failure, the correct Release
   configuration where applicable, and zero-test rejection.
4. Run native application self-test.
5. Run native UI-performance gate.
6. Package from a clean stage/archive.
7. Run size, hash, import/runtime, Unicode, required-file, and documentation
   inspection.
8. Assert all evidence files and stable success content.
9. Run docs/artifact consistency.
10. Upload only after every blocking step passed.

The existing MinGW helper intentionally builds only `svm-native-win32`.
Therefore local cross validation must still run a separate full:

```text
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure --no-tests=error
```

## Evidence Vocabulary

Use these meanings consistently:

- `GateStatus=passed`: a local PTY harness actually ran every requested
  scenario and passed.
- `GateStatus=documented-local-only`: the Windows workflow records the required
  local PTY boundary but did not execute a POSIX PTY.
- `Classification=local-only-release-candidate-evidence`: Wine/PTTY result, not
  physical-driver or hosted Windows evidence.
- `CiExecutesPtyMatrix=no`: explicit protection against a false CI claim.
- `Gate status: passed`: the package inspector completed all package gates.
- `Wine gate status: passed` with `Wine gate strict: 1`: the strict local Wine
  self-test and UI-performance command succeeded.

## Validation Baseline Observed During Research

- Clean host configure succeeded and registered 27 tests.
- The existing full MinGW tree registered 34 tests.
- `python3 scripts/check-docs-artifact-consistency.py` printed
  `docs consistency ok` before Task 05 edits.
- `bash -n scripts/package-windows-native-mingw.sh` passed.
- Task 04 commit `d911f70` defines seven PTY scenarios and retains explicit
  local-only evidence classification.

These are inventory checks, not substitutes for Task 05's full build, CTest,
Wine, PTY, package, and independent review gates.

## Non-Goals

- No TCP, UDP, Qt, SQLite, plugin, or new runtime dependency.
- No overlapped serial backend and no `CancelIoEx` addition.
- No renaming of established artifact paths.
- No claim that Wine/PTTY equals physical hardware.
- No claim that the Windows GitHub runner executes POSIX PTY scenarios.
- No duplicate test targets or compatibility wrappers when the existing source
  graph already provides the required coverage.
