# Final Artifact Review

Date: 2026-07-01T02:22:00+08:00
Target commit: `b7a15fe7a487f22f8f30f75008d847effe40a2c3`

## Source Runs

| Evidence | Run id | Artifact | Local path |
|---|---:|---|---|
| Package artifact | `28465681222` | `SerialValueMatcherNative-win32-native-x64` | `artifacts/github-actions/final-package-28465681222/` |
| UI capture artifact | `28465775335` | `windows-native-ui-screenshots` | `artifacts/github-actions/final-ui-28465775335/` |

Both source runs completed with `success`; see
`.workflow/native-ui-performance-stability/context/final-actions-review.md`.

## Package Artifact

- Zip: `artifacts/github-actions/final-package-28465681222/SerialValueMatcherNative-win32-native-x64.zip`
- Zip bytes: `472968`
- Zip SHA256: `A7DDF98FFFAA4E5207C3D68215A6FACB1F006DF3B89FFC1F1309376F732EC336`
- Local `sha256sum`: `a7ddf98fffaa4e5207c3d68215a6facb1f006df3b89ffc1f1309376f732ec336`
- SHA256 result: matched
- Extracted exe: `artifacts/github-actions/final-package-28465681222/extracted/svm-native-win32.exe`
- Executable type: `PE32+ executable (GUI) x86-64, for MS Windows`
- Executable bytes: `972288`
- Executable SHA256: `9aa86fc4ce0b431744ca7136bcb44ccfe660133cccb72a04dbe810933f236282`

Zip contents:

- `svm-native-win32.exe`
- `README.md`
- `docs/windows-deployment.md`
- `docs/windows-native-slimming.md`
- `docs/windows-native-ui-validation.md`
- `docs/windows-serial-validation.md`

Package summary gate:

- Native exe present: yes
- Forbidden Qt/SQLite/.NET runtime files: none
- Unicode text probe: passed
- Gate status: passed

Action logs in artifact:

- `native-self-test.log`: `ok`
- `native-ui-perf-test.log`: `ui-perf ok tabs=300 tab-ms=1110 layout-pass=12 apply=324 revision=12 log-lines=1200 log-ms=28 log-flush=2 log-rebuild=1`
- `serial-pty-matrix.txt`: records `Status: local-only` because the Windows runner does not expose POSIX PTYs or Wine dosdevices. This is a recorded runner limitation, not a package workflow failure; the package workflow step completed successfully and the local PTY matrix command is preserved in the artifact.

## UI Capture Artifact

Original screenshot count: `22`.

Captured scenarios:

- Default window: `root.png`
- DPI smoke: `dpi-100-window.png`, `dpi-125-window.png`
- Standard tabs: `tab-single.png`, `tab-quick.png`, `tab-file.png`, `tab-scan.png`, `tab-settings.png`
- Compact tabs: `compact-tab-single.png`, `compact-tab-quick.png`, `compact-tab-file.png`, `compact-tab-scan.png`, `compact-tab-settings.png`
- Resize sweep: `resize-0.png` through `resize-4.png`
- Splitter drag frames: `log-splitter-before.png`, `log-splitter-frame-01.png`, `log-splitter-frame-02.png`, `log-splitter-after.png`

`capture-status.txt` gate:

- `PASS default-window`
- `PASS dpi-smoke-100`
- `PASS dpi-smoke-125`
- `PASS tab-tab-set screenshots=5 switching=clicked-frame-diff-validated-by-ui-perf`
- `PASS compact-tab-tab-set screenshots=5 switching=clicked-frame-diff-validated-by-ui-perf`
- `PASS resize-sweep screenshots=5`
- `PASS splitter-drag-frames ... live=true diff-gated=true`
- `PASS capture-complete`

`window-info.txt`:

- Window title: `串口值匹配器 Win32 Native`
- Default size: `1212x753`
- Compact size: `760x520`
- Window DPI: `96`
- Captured at: `2026-06-30T18:10:57.8006374+00:00`

`ui-perf-test.log` in the screenshot artifact says the screenshot script skipped duplicate UI perf execution because the workflow already ran the independent UI performance gate before capture.

## Screenshot Integrity Review

Static image checks:

- All original PNG files are valid `8-bit/color RGBA, non-interlaced` PNGs.
- Image entropy range observed across screenshots: `0.255477` to `0.335556`.
- Image standard deviation range observed across screenshots: `0.191970` to `0.246999`.
- Representative pixel diffs:
  - `tab-single.png` vs `tab-file.png`: `27476` differing pixels.
  - `log-splitter-before.png` vs `log-splitter-frame-02.png`: `52691` differing pixels.
  - `resize-1.png` vs `resize-4.png`: `599295` differing pixels.

Visual review sheets generated locally:

- `artifacts/github-actions/final-ui-28465775335/review-tabs-sheet.png`
- `artifacts/github-actions/final-ui-28465775335/review-compact-tabs-sheet.png`
- `artifacts/github-actions/final-ui-28465775335/review-resize-dpi-sheet.png`
- `artifacts/github-actions/final-ui-28465775335/review-splitter-sheet.png`

Visual findings:

- Standard and compact tab pages are populated; no tab page was observed as blank.
- Resize/DPI captures did not show black, white, or gray broken blocks.
- Splitter drag frames show live height changes without obvious stale-frame corruption.
- Current-page prompt text remains visible in the captured states.

## Local Wine Smoke

The downloaded `svm-native-win32.exe` was also tested locally with Wine/Xvfb:

- `xvfb-run -a wine ... --self-test`: exit code `0`
- Local self-test log: `ok`
- `xvfb-run -a wine ... --ui-perf-test`: exit code `0`
- Local UI perf log: `ui-perf ok tabs=300 tab-ms=2721 layout-pass=12 apply=324 revision=12 log-lines=1200 log-ms=314 log-flush=2 log-rebuild=1`

Wine emitted non-blocking runtime noise during UI perf (`KiUserCallbackDispatcher ignoring exception` and X connection shutdown messages), but the process exit code and application log both reported success.

## Task 02 Gate

- Final package artifact downloaded: passed
- Final UI capture artifact downloaded: passed
- `svm-native-win32.exe` located and inspected: passed
- ZIP SHA256 verified: passed
- Package audit summary: passed
- UI capture status: passed
- Screenshot integrity and visual review: passed
- Local Wine self-test/UI perf smoke: passed

No blocking final artifact issue was found.
