# Artifact Baseline

Generated: 2026-06-30T06:49:45+08:00

## Scope

This baseline records the latest known local and GitHub Actions artifact evidence visible at the start of Phase 1. It is not the final release approval. Missing evidence is marked `unknown` unless an explicit gate failure is present.

## Repository And Artifact Commit Split

| Field | Value |
|---|---|
| Local repository HEAD at capture time | `4e1d18f9355203ee313c1e8f6bf11e76742f01a9` |
| Latest product package artifact Commit | `c4c8b272bf824ef027d5f042dfd736d09aa6231c` |
| Reason for difference | Local HEAD includes workflow audit documentation commits after the product-code artifact commit. |

## Latest Windows Native Package Run

| Field | Value |
|---|---|
| Run | `28387782130` |
| URL | `https://github.com/D1654/serial-value-matcher-native/actions/runs/28387782130` |
| Display title | `fix: reduce native drag redraw flicker` |
| Branch | `main` |
| Commit | `c4c8b272bf824ef027d5f042dfd736d09aa6231c` |
| Created | `2026-06-29T16:38:59Z` |
| Updated | `2026-06-29T16:44:05Z` |
| Status | `completed` |
| Conclusion | `success` |
| Job | `Build, self-test, and size-gate Win32 native app` |
| Job URL | `https://github.com/D1654/serial-value-matcher-native/actions/runs/28387782130/job/84106828810` |

### Package Job Steps

All recorded job steps completed successfully:

1. Check out repository
2. Configure native-only
3. Build Release
4. Run native tests
5. Run native app self-test
6. Run native UI performance gate
7. Package native zip
8. Report native package size
9. Upload native artifact

### Local Downloaded Package Evidence

| Field | Value |
|---|---|
| Artifact directory | `artifacts/github-actions/windows-native-28387782130/` |
| Artifact name | `SerialValueMatcherNative-win32-native-x64` |
| Zip path | `artifacts/github-actions/windows-native-28387782130/SerialValueMatcherNative-win32-native-x64/SerialValueMatcherNative-win32-native-x64.zip` |
| Zip SHA256 file | `artifacts/github-actions/windows-native-28387782130/SerialValueMatcherNative-win32-native-x64/SerialValueMatcherNative-win32-native-x64.zip.sha256.txt` |
| Package summary path | `artifacts/github-actions/windows-native-28387782130/SerialValueMatcherNative-win32-native-x64/SerialValueMatcherNative-win32-native-x64.package-summary.txt` |
| Extracted exe path | `artifacts/github-actions/windows-native-28387782130/extracted/svm-native-win32.exe` |
| Zip bytes | `465659` |
| Extracted bytes | `971592` |
| File count | `6` |
| exe bytes | `957440` |
| Zip SHA256 | `19ACECE40D716C92FD65BB4EC832D84975C09EA09B3B9A838C25F333F08A9EA9` |
| Extracted exe SHA256 | `BC8050E29AA8C72A4C1AE8269A8576972FB5BD46AFD87C1271432CFDB54BED05` |
| Gate status | `passed` |
| Forbidden Qt/SQLite runtime files | `none` |
| Unicode text probe | `passed` |

### Package Gate Outcome

| Gate | Outcome | Evidence |
|---|---|---|
| Build Release | `passed` | GitHub Actions job step success. |
| Native CTest | `passed` | GitHub Actions job step success. |
| self-test | `passed` | GitHub Actions job step success. |
| ui-perf | `passed` | GitHub Actions job step success. |
| Package audit | `passed` | Package summary `Gate status: passed`. |
| Forbidden runtime package check | `passed` | Package summary says forbidden Qt/SQLite runtime files: `none`; Python local checker also covers .NET imports when run on local MinGW package. |

## Latest Windows Native UI Screenshot Run

| Field | Value |
|---|---|
| Run | `28009274956` |
| URL | `https://github.com/D1654/serial-value-matcher-native/actions/runs/28009274956` |
| Display title | `Windows Native UI Capture` |
| Branch | `main` |
| Commit | `e7a249a63d1bf0686aacd20cabe58f2e4d8ec450` |
| Created | `2026-06-23T07:19:04Z` |
| Updated | `2026-06-23T07:21:45Z` |
| Status | `completed` |
| Conclusion | `success` |
| Job | `Capture Win32 native UI on Windows` |
| Job URL | `https://github.com/D1654/serial-value-matcher-native/actions/runs/28009274956/job/82898386359` |
| Baseline freshness | `stale relative to current product artifact` |

### Screenshot Evidence

| Field | Value |
|---|---|
| Artifact directory | `artifacts/github-actions/windows-ui-28009274956/` |
| capture-status | `ok` |
| Window title | `串口值匹配器 Win32 Native` |
| Exe path in workflow | `D:\a\serial-value-matcher-native\serial-value-matcher-native\build-windows-native-ui\Release\svm-native-win32.exe` |
| Default size | `1212x753` |
| Compact size | `760x520` |
| Captured at | `2026-06-23T07:21:30.0261240+00:00` |
| UI perf log | `UI 性能门禁已由 workflow 独立步骤执行，本截图脚本跳过重复执行。` |

Screenshot files present:

- `root.png`
- `tab-single.png`
- `tab-quick.png`
- `tab-file.png`
- `tab-scan.png`
- `tab-settings.png`
- `compact-tab-single.png`
- `compact-tab-quick.png`
- `compact-tab-file.png`
- `compact-tab-scan.png`
- `compact-tab-settings.png`

### UI Screenshot Outcome

| Gate | Outcome | Evidence |
|---|---|---|
| Windows UI capture workflow | `passed` for run `28009274956` | Job conclusion success, `capture-status.txt` is `ok`. |
| Screenshot freshness for current HEAD/package | `unknown` | The successful UI capture commit is older than current package commit `c4c8b27...`. |
| DPI 100%/125% matrix | `unknown` | Current workflow does not record a DPI matrix baseline. |
| Splitter drag frame/delta evidence | `unknown` | Current screenshot artifact does not include dedicated drag-frame evidence. |

## Local MinGW Diagnostic Package

| Field | Value |
|---|---|
| Package directory | `artifacts/windows-native-mingw/` |
| Zip path | `artifacts/windows-native-mingw/SerialValueMatcherNative-win32-native-x64-mingw.zip` |
| Package summary path | `artifacts/windows-native-mingw/SerialValueMatcherNative-win32-native-x64-mingw.package-summary.txt` |
| Extracted exe path | `artifacts/windows-native-mingw/SerialValueMatcherNative-win32-native-x64-mingw/svm-native-win32.exe` |
| Local build exe path | `build-windows-native-mingw/svm-native-win32.exe` |
| Zip bytes | `697729` |
| Extracted bytes | `1804411` |
| File count | `7` |
| Native exe sha256 | `25B886D75CCA5088E78CFDC3C8C21DBBBF5B334C077C56BAF7F4497FF9FC7508` |
| Gate status | `passed` |
| Wine gate status | `passed` |
| Wine gate strict | `1` |

Local MinGW is diagnostic only. It does not replace the Windows Actions MSVC package.

## Serial PTY Evidence

| Field | Value |
|---|---|
| PTY script | `scripts/run-windows-native-serial-pty-loopback.py` |
| Test executable expected by script | `build-windows-native-mingw/native_win32_serial_loopback_tests.exe` |
| Current Actions package workflow PTY step | `unknown` / not present in latest package job steps |
| Local persisted PTY result in `artifacts/**` | `unknown`; no dedicated PTY result artifact found during this task |
| Required future gate | Phase 4 Task 04 must add/confirm normal, reopen, timeout, and cancel PTY edge matrix. |

## Comparison With Active Workflows

| Workflow | Current Evidence Match |
|---|---|
| `.github/workflows/windows-native-package.yml` | Current latest successful package run matches the active workflow steps, including native tests, self-test, ui-perf, package audit, summary, and upload. |
| `.github/workflows/windows-native-ui-capture.yml` | A successful UI capture artifact exists, but it is stale relative to the latest package artifact and current local HEAD. |
| PTY loopback | Script/test support exists, but not proven as a current Actions package artifact gate. |

## Evidence To Regenerate Later

Phase 6 must regenerate and download final evidence after all code/docs changes:

- Current Windows native package artifact from GitHub Actions.
- Current package summary, zip SHA256, and extracted executable SHA256.
- Current extracted `svm-native-win32.exe`.
- Current Windows UI screenshots from the same or later commit than the package artifact.
- `--self-test` and `--ui-perf-test` logs for the final executable.
- Serial PTY normal/reopen/timeout/cancel evidence after Phase 4 adds the matrix.
- Package audit evidence including forbidden runtime, Unicode probe, zip bytes, extracted bytes, and file count.
- Documentation consistency output after Phase 5.

## Baseline Conclusion

The latest package artifact for product commit `c4c8b272bf824ef027d5f042dfd736d09aa6231c` is a successful Windows native package baseline with passing native tests, self-test, ui-perf, and package audit. The latest successful Windows UI screenshot baseline is older (`e7a249a...`) and must not be treated as proof that the current package UI is correct. PTY edge-case evidence is currently `unknown` at the artifact level and must be made explicit later.
