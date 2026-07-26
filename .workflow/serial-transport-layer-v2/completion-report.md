# Serial Transport Layer v2 Completion Report

Date: 2026-07-25
Project: SerialValueMatcher Native
Scope: serial-only unified transport foundation, Win32 ownership, queue and
settlement hardening, structured evidence, and architecture closure

## Final Verdict

Status: **PASSED**

All mandatory host, MinGW/Wine, seven-scenario PTY, strict package, boundary,
documentation, and hosted Windows gates passed. The serial transport v2
workflow is ready to close. This result does not claim physical PLC or
USB-to-serial hardware coverage and does not add TCP, Qt compatibility, or a
general Gray/variable-bit-layout codec.

## Verification Identity

- Final implementation target: `2f694804fd13370128d41752d74f5de07d702d4a`
- Branch: `main`
- Remote: `origin` (`https://github.com/D1654/serial-value-matcher-native.git`)
- Project version: `1.0.4` (`v1.0.4`)
- Native package artifact: `SerialValueMatcherNative-win32-native-x64`
- MinGW package artifact: `SerialValueMatcherNative-win32-native-x64-mingw`
- UI artifact: `windows-native-ui-screenshots`
- Local evidence root: `artifacts/local/serial-transport-v2-final/`
- Hosted evidence root: `artifacts/github-actions/serial-transport-v2-final/`
- Task 03 report, research, context, and state files are deliberately excluded
  from the implementation target. Their final commit is metadata-only and does
  not change the verified product tree.

Implementation-target remediation commits:

- `f593391` — enable assertions in Release test targets and harden CRLF/UTF-8
  version plus open-file cleanup tests.
- `4fc5c5a` — remove stale Wine UI coordinate defaults and begin using the real
  Win32 tab-control geometry.
- `2f69480` — remove the invalid cross-process item-rectangle query and use the
  real tab-control rectangle with the existing fixed tab centers.

## Environment

| Item | Observed value |
|---|---|
| Host | Linux 6.1.0-44-amd64 x86_64 |
| CMake / CTest | 3.25.1 / 3.25.1 |
| Ninja | 1.11.1 |
| Python | 3.11.2 |
| MinGW compiler | GCC 12-win32 |
| Wine | wine-8.0 (Debian 8.0~repack-4) |
| GitHub CLI | 2.23.0 |
| Git | 2.39.5 |
| Package/PTY Wine prefix | `/root/.wine` |
| Final local UI prefix | `/tmp/svm-transport-v2-ui-direct-control-20260722` (fresh) |

## Local Verification Matrix

| Gate | Result | Exit | Duration | Evidence |
|---|---:|---:|---:|---|
| Fresh host configure | passed | 0 | 1 s | `host-configure.log` |
| Fresh host build | passed | 0 | 110 s | `host-build.log` |
| Focused host transport CTest | 9/9 passed | 0 | <1 s | `host-focused-ctest.log` |
| Full host CTest | 29/29 passed | 0 | 2 s | `host-ctest.log` |
| Host Release assertion remediation | 29/29 passed | 0 | 21 s final run | `ci-remediation-host-release-final.log` |
| MinGW helper build | passed/up to date | 0 | <1 s | `mingw-helper-build.log` |
| Complete MinGW build | passed/up to date | 0 | <1 s | `mingw-build.log` |
| Focused MinGW transport CTest | 11/11 passed | 0 | 33 s | `mingw-focused-ctest.log` |
| Full MinGW CTest | 36/36 passed | 0 | 126 s | `mingw-ctest.log` |
| MinGW Release assertion remediation | 36/36 passed | 0 | 126 s | `ci-remediation-mingw-release-final2.log` |
| Seven-scenario Wine/PTY matrix | passed, 5006 transactions | 0 | 33 s | `serial-pty-matrix.log` |
| Strict Wine/package gate | passed | 0 | 30 s | `package.log` |
| Fresh-prefix Wine UI capture | passed, 30 PNG | 0 | 84 s evidence interval | `ui-direct-control-fresh/` |
| Static repository gates | passed | 0 | 2 s | `static-gates.log` |

The Release test compile commands contain `-DNDEBUG -UNDEBUG`; therefore the
assert-based test executables remain active in Release. Targeted
`version_metadata_tests` and `native_file_send_state_tests` pass before the
complete 29-test host and 36-test MinGW suites.

## Reproducible Commands

```bash
cmake -S . -B /tmp/svm-transport-v2-final -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-final --parallel
ctest --test-dir /tmp/svm-transport-v2-final --output-on-failure --no-tests=error

scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure --no-tests=error

SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress,close,stale \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/serial-transport-v2-final/serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py

SVM_WINEPREFIX=/root/.wine SVM_STRICT_WINE_TEST=1 \
bash scripts/package-windows-native-mingw.sh

SVM_WINEPREFIX=/tmp/svm-transport-v2-ui-direct-control-20260722 \
SVM_WINE_UI_OUTPUT_DIR=artifacts/local/serial-transport-v2-final/ui-direct-control-fresh \
scripts/capture-windows-native-ui-wine.sh

python3 scripts/check-transport-boundaries.py --self-test
python3 scripts/check-transport-boundaries.py
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

## PTY Evidence

- `GateStatus=passed`
- `Classification=local-only-release-candidate-evidence`
- `Scenarios=normal,reopen,timeout,cancel,stress,close,stale`
- `Transactions=5006`
- `TransactionsMeaning=completed-request-response-exchanges`
- `ComPort=COM5`
- `PtyInstanceCount=7`
- `PtyIsolation=per-scenario`
- `Transport=serial-adapter-contract`
- `WinePrefix=/root/.wine`
- `CiExecutesPtyMatrix=no`

This historical artifact is bound to implementation target `2f69480`, where the
final scenario was labeled `stale` even though it completed the old exchange
before close/reopen and did not inject a late old completion. Post-completion
review renamed the current scenario to `reopen-generation-isolation`; current
seven-scenario evidence is recorded in the bug-fixer review rather than
rewriting this historical artifact identity.

Evidence:

- `artifacts/local/serial-transport-v2-final/serial-pty-matrix.log`
- `artifacts/local/serial-transport-v2-final/serial-pty-matrix-summary.txt`

## Local Package Evidence

- ZIP: `SerialValueMatcherNative-win32-native-x64-mingw.zip`
- ZIP bytes: `919978` (limit `5242880`)
- Extracted bytes: `2288787` (limit `8388608`)
- ZIP SHA256: `0AF463B896D97243AFF666F3DE4D9D6862534C905232213C482C5E5F84B33BD8`
- Native EXE bytes: `2166784`
- Native EXE SHA256: `5DF907DD1EB27117BEFA76044F751BC68C436661DAF20802B4A2A3EAED3FF1F2`
- Required executable, README, and 13-document set: passed
- Package documentation links and exact file set: passed
- PE imports: passed; no forbidden Qt, SQLite, or .NET runtime
- Unexpected DLL files: none
- Unicode probe: passed
- `Wine gate status: passed`
- `Wine gate strict: 1`
- `Gate status: passed`

Evidence:

- `artifacts/local/serial-transport-v2-final/package/SerialValueMatcherNative-win32-native-x64-mingw.package-summary.txt`
- `artifacts/local/serial-transport-v2-final/package/SerialValueMatcherNative-win32-native-x64-mingw.zip.sha256.txt`

## UI Evidence

Local Wine capture changed during final remediation and was therefore rerun.
Two retained calibration attempts failed before the fixed defaults were
selected. A subsequent run passed, and a separate fresh-prefix run also passed
without coordinate overrides:

- `PngCount=30`
- `MissingStatusTerms=none`
- `HasFailStatus=no`
- `SelfTestStatus=passed`
- `UiPerfStatus=passed`
- `WindowInfoPresent=yes`
- `GateStatus=passed`
- Default and compact five-tab sets, resize sweep, live splitter frames, and
  phase-1 regression closure: passed

The hosted Windows script no longer duplicates the application layout formula.
It locates the actual `SysTabControl32` window rectangle; tab clicks use the
existing fixed item centers, and the 12-pixel splitter target is derived from
the real tab-control top edge.

Evidence:

- `artifacts/local/serial-transport-v2-final/ui-direct-control-fresh/ui-evidence-summary.txt`
- `artifacts/local/serial-transport-v2-final/ui-direct-control-fresh/capture-status.txt`

## Hosted Windows Evidence

| Workflow | Run ID | Event | Head SHA | Conclusion | Job duration | Artifact |
|---|---:|---|---|---|---:|---|
| `windows-native-package.yml` | `29914919999` | push | `2f694804fd13370128d41752d74f5de07d702d4a` | success | 4m58s | `SerialValueMatcherNative-win32-native-x64` |
| `windows-native-ui-capture.yml` | `29914927486` | workflow_dispatch | `2f694804fd13370128d41752d74f5de07d702d4a` | success | 5m22s | `windows-native-ui-screenshots` |

Run URLs:

- `https://github.com/D1654/serial-value-matcher-native/actions/runs/29914919999`
- `https://github.com/D1654/serial-value-matcher-native/actions/runs/29914927486`

Package workflow evidence:

- Native CTest: 36/36 passed
- Native self-test: `ok`
- UI performance: `ui-perf ok`, 300 tab switches
- Backend closure: queue, exactly-once, typed errors, generation isolation,
  and local PTY fault evidence recorded
- Hosted PTY boundary: `GateStatus=documented-local-only`,
  `CiExecutesPtyMatrix=no`
- Package audit: `Gate status: passed`

UI workflow evidence:

- All build, 36-test CTest, self-test, performance, screenshot, completeness,
  documentation, and upload steps passed
- `PngCount=22`
- `MissingStatusTerms=none`
- `HasFailStatus=no`
- `SelfTestStatus=passed`
- `UiPerfStatus=passed`
- `GateStatus=passed`

Hosted artifact metadata:

| Artifact | ID | Service archive bytes | Service digest |
|---|---:|---:|---|
| `SerialValueMatcherNative-win32-native-x64` | `8527570256` | `629328` | `sha256:1f2e33cb4dd269d050d75f0ef2d307836b646bd58acd3fb2220fcf7e63dd5d27` |
| `windows-native-ui-screenshots` | `8527583871` | `475156` | `sha256:e2971bddf3c60cb0dc2121c055f4de9e9ea83f380ff9a309ab725741d3c1dc14` |

## Downloaded Artifact Audit

Downloaded package path:
`artifacts/github-actions/serial-transport-v2-final/package-29914919999/`

- Evidence files: package ZIP, SHA sidecar, package summary, native CTest,
  self-test, UI-performance, backend closure, and two PTY boundary files
- Embedded ZIP bytes: `624534`
- Embedded ZIP SHA256: `46CEA245B57B828CB0655297E220023B9ABEF59DAC8B55280A203D523909BC65`
- Sidecar match: yes
- Extracted bytes: `1282395`
- File count: `15`
- Native EXE bytes: `1158144`
- Native EXE SHA256: `01A9770FFA9AF830DF0C6794A2F31BC9BF4A5BE55A4DD9A00ED166F7D5541572`
- Required files, imports, forbidden-runtime checks, Unicode probe,
  documentation links, and documentation file set: passed

The downloaded ZIP was extracted with `7z` to preserve its UTF-8 Chinese file
names. Python reinspection was executed from an exact
`2f694804fd13370128d41752d74f5de07d702d4a` checkout with Windows CRLF
conversion so its byte-for-byte documentation comparison matches the hosted
Windows build checkout. The result is `Gate status: passed` in
`package-29914919999/reinspection-windows-checkout-summary.txt`.

Downloaded UI path:
`artifacts/github-actions/serial-transport-v2-final/ui-29914927486/`

- File count: `27`
- PNG count: `22`
- Required logs/status/window information: present and nonempty
- Capture summary: `GateStatus=passed`

## Remediation History

Failed evidence was retained and not promoted:

1. Target `6faa2ff`: package run `29754964523` and UI run `29754986970`
   each reached 35/36 tests; Release had disabled assert-based tests and the
   version parser rejected Windows CRLF. Commit `f593391` fixed the test gate.
2. Target `f593391`: package run `29757481724` passed; UI run `29757502545`
   exposed the stale compact-tab coordinate formula.
3. Target `4fc5c5a`: package run `29896059306` passed; UI run `29896069562`
   rejected the attempted cross-process `TCM_GETITEMRECT` pointer message.
4. Target `2f69480`: the pointer message was removed rather than wrapped in
   compatibility code. Package run `29914919999` and UI run `29914927486`
   both passed.

## Static Scope Assertions

- Boundary checker self-test and repository scan: passed
- Documentation consistency: passed
- Removed `SerialTransport` and `Win32SerialPort` files/definitions/includes:
  absent
- Qt includes, Qt types, and Qt CMake linkage in active native/transport scope:
  absent
- TCP, UDP, and socket implementation added by this workflow: absent
- General Gray decoder or variable bit-layout codec in transport: absent
- Existing fixed `Gray16`: remains only in upper-layer analysis
- `git diff --check`: passed
- Unrelated untracked workflow material: preserved and explicitly unstaged
- Mandatory BS-1 was found as an old pending governance marker during final
  review. A transparent retrospective completeness audit is recorded in
  `brainstorm/bs-1.md`; it changes no product requirement or implementation.

## Known Limitations

- Wine/PTY is local release-candidate evidence for the serial adapter
  contract, not proof of a physical Windows vendor driver, PLC, or USB-serial
  converter.
- Real PLC/USB-serial smoke testing, hardware flow control, electrical faults,
  unplug/replug behavior, and long-duration device sessions remain optional
  hardware coverage.
- The one-second cancellation/terminal-result target is verified only for the
  deterministic native and Wine environments covered by this workflow.
- Hosted Windows CI documents but does not execute the Linux/Wine POSIX PTY
  matrix (`CiExecutesPtyMatrix=no`).
- This workflow deliberately implements no TCP/UDP transport, Qt compatibility,
  general Gray decoder, variable bit-layout codec UI, or codec persistence
  format.

## Closure Checklist

- [x] Fresh host focused and full CTest pass.
- [x] Complete MinGW build, focused CTest, and full CTest pass.
- [x] Seven-scenario PTY matrix and summary assertions pass.
- [x] Strict Wine self-test/UI-performance and package inspection pass.
- [x] Boundary, documentation, forbidden-scope, diff, and status gates pass.
- [x] Hosted package and UI runs match the implementation target SHA and pass.
- [x] Downloaded artifacts contain every required evidence file and pass
  platform-correct reinspection.
- [x] Automated evidence is distinguished from optional physical hardware
  coverage.
- [x] Workflow state may now be marked complete.
