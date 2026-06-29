# Documentation Consistency Gate Design

Generated: 2026-06-30T06:57:41+08:00

## Objective

Define the Phase 5 documentation/source/artifact consistency checker before it
is implemented. The checker must catch public documentation that contradicts
the current Win32 native release route or claims artifact facts without evidence.

## Target Script

Primary implementation:

- `scripts/check-docs-artifact-consistency.py`

Optional wrapper:

- `scripts/check-docs-artifact-consistency.ps1`

Python is the primary script because it can run on Windows GitHub Actions,
Linux developer machines, and local MinGW/Wine environments without binding the
check to PowerShell-only parsing. A PowerShell wrapper may be added only to make
Windows release workflow calls shorter.

## Inputs

The checker should accept these command-line inputs:

| input | Required | Description |
|---|---|---|
| `--repo-root <path>` | no | Repository root. Defaults to current working directory. |
| `--docs-root <path>` | no | Public documentation root. Defaults to `docs`. |
| `--readme <path>` | no | README path. Defaults to `README.md`. |
| `--package-summary <path>` | no in local mode, yes in release mode | Final package summary from `scripts/package-windows-native.ps1` or MinGW package helper. |
| `--artifact-name <name>` | no | Expected GitHub Actions artifact name. Defaults to `SerialValueMatcherNative-win32-native-x64`. |
| `--mode <local|ci|release>` | no | Check strictness. Defaults to `local`. |
| `--json <path>` | no | Optional machine-readable report output path. |

Repository files the checker reads automatically:

- `CMakeLists.txt`
- `.github/workflows/windows-native-package.yml`
- `.github/workflows/windows-native-ui-capture.yml`
- `scripts/package-windows-native.ps1`
- `scripts/inspect-windows-package.ps1`
- `scripts/capture-windows-native-ui.ps1`
- `scripts/run-windows-native-serial-pty-loopback.py`
- `README.md`
- `native-spec.md` if still present
- `docs/**/*.md`

Optional artifact inputs:

- package summary text file
- UI capture metadata such as `capture-status.txt` and `window-info.txt`
- SHA256 text file if the release workflow passes it explicitly in Phase 6

## Mechanically Checkable Claims

### Required Current Facts

The checker should verify that current docs and source agree on these constants:

| Claim | Expected Value | Evidence |
|---|---|---|
| Release executable | `svm-native-win32.exe` | CMake target and native package script. |
| Native package artifact | `SerialValueMatcherNative-win32-native-x64` | Windows native package workflow. |
| Native package zip | `SerialValueMatcherNative-win32-native-x64.zip` | Package script and package summary. |
| Primary build workflow | `.github/workflows/windows-native-package.yml` | Repository workflow file. |
| UI capture workflow | `.github/workflows/windows-native-ui-capture.yml` | Repository workflow file. |
| Native self-test flag | `--self-test` | `src/win32/main.cpp` and workflows. |
| Native UI perf flag | `--ui-perf-test` | `src/win32/main.cpp` and workflows. |
| Package audit script | `scripts/inspect-windows-package.ps1` | Native package script. |
| UI capture script | `scripts/capture-windows-native-ui.ps1` | UI capture workflow. |
| Serial PTY script | `scripts/run-windows-native-serial-pty-loopback.py` | Source inventory and future CI gate. |

### Forbidden Or High-Risk Doc Claims

These patterns should produce a hard fail in `ci` and `release` mode when they
appear in public docs outside a clearly labeled `legacy` or `future-plan`
section:

- Claiming `native-spec.md` is a current source of truth.
- Claiming `--self-test` checks for missing Qt, SQLite, or .NET runtime files.
- Presenting Qt workflow/test success as proof of Win32 native release UI
  correctness.
- Saying the current release package is a Qt package or `svm-native.exe`.
- Claiming a "latest" run, size, SHA256, screenshot, or release status without
  a nearby Actions run id, package summary path, or artifact evidence link.
- Quoting package sizes as current when no package summary is provided.
- Using `unknown` as a user-facing shipped feature label.

### Warning-Level Claims

These should produce warnings, not failures, unless `--mode release` promotes
them to failures:

- `future-plan` labels in user-facing docs.
- `manual-optional` sections that are not isolated to testing or
  troubleshooting.
- Legacy Qt notes outside `docs/legacy-qt-notes.md`.
- Old doc filenames that remain without a replacement link.
- Missing screenshot freshness metadata when screenshots are described as
  illustrative rather than current proof.

## Link And Document Set Checks

The checker should validate that the public documentation set matches the IA:

- `README.md` links to:
  - `docs/user-guide.md`
  - `docs/developer-guide.md`
  - `docs/win32-native-architecture.md`
  - `docs/testing-validation.md`
  - `docs/release-artifacts.md`
  - `docs/troubleshooting.md`
  - `docs/legacy-qt-notes.md`
- Each linked file exists.
- Old documents either no longer exist or contain a replacement link:
  - `docs/architecture.md`
  - `docs/windows-deployment.md`
  - `docs/windows-native-local-debug.md`
  - `docs/windows-native-parity.md`
  - `docs/windows-native-slimming.md`
  - `docs/windows-native-ui-validation.md`
  - `docs/windows-serial-validation.md`
- `docs/images/native-ui-overview.png` is referenced only with freshness
  metadata or is marked illustrative/legacy.

## Package Summary Checks

When `--package-summary` is provided, the checker should parse the text and
verify:

- `Gate status: passed`
- package zip name contains `SerialValueMatcherNative-win32-native-x64`
- executable entry is `svm-native-win32.exe`
- forbidden Qt/SQLite runtime files are `none`
- Unicode text probe is `passed`
- zip bytes and extracted bytes are present
- file count is present

If the package summary is missing in `release` mode, hard fail. If it is missing
in `local` mode, emit a warning and skip artifact-derived checks.

## Output Format

Console output should be deterministic and grep-friendly:

```text
docs-consistency: mode=ci
docs-consistency: input README.md
docs-consistency: input docs
docs-consistency: check executable-name PASS
docs-consistency: check package-artifact PASS
docs-consistency: warning legacy-link docs/windows-native-parity.md replacement link only
docs-consistency: fail stale-self-test-runtime-claim docs/testing-validation.md:42
docs-consistency: summary errors=1 warnings=1 checks=27
```

JSON output should use this shape when `--json` is supplied:

```json
{
  "mode": "ci",
  "checks": 27,
  "errors": [
    {
      "id": "stale-self-test-runtime-claim",
      "path": "docs/testing-validation.md",
      "line": 42,
      "message": "Forbidden runtime checks belong to package audit, not --self-test."
    }
  ],
  "warnings": [
    {
      "id": "legacy-link",
      "path": "docs/windows-native-parity.md",
      "line": 1,
      "message": "Legacy document is retained as a replacement link only."
    }
  ]
}
```

Exit codes:

| Code | Meaning |
|---:|---|
| 0 | All checks passed; warnings may exist in `local` and `ci` mode. |
| 1 | One or more hard-fail checks failed. |
| 2 | Invalid input, missing required file in selected mode, or unreadable package summary. |

## Failure Behavior

| Condition | local | ci | release |
|---|---|---|---|
| Active Win32 native contradiction | fail | fail | fail |
| Missing required target doc | fail | fail | fail |
| Broken README link | fail | fail | fail |
| Stale self-test forbidden-runtime claim | fail | fail | fail |
| Qt evidence used as Win32 native release proof | fail | fail | fail |
| Missing package summary | warning | warning | fail |
| `future-plan` in public docs | warning | warning | fail if in README or user-guide |
| Legacy doc retained with replacement link | warning | warning | warning |
| Optional manual serial validation note | pass if labeled `manual-optional` | pass if labeled `manual-optional` | pass if labeled `manual-optional` |

Hard failures must block release artifacts. Warnings should be visible in the
Actions log and JSON output so they can be promoted later if needed.

## CI Integration Points

### Windows Native Package Workflow

Add a step after `Package native zip` and before `Upload native artifact`:

```yaml
- name: Check documentation and artifact consistency
  shell: pwsh
  run: |
    python scripts/check-docs-artifact-consistency.py `
      --mode release `
      --package-summary "$env:PACKAGE_OUTPUT_DIR\$env:PACKAGE_NAME.package-summary.txt" `
      --artifact-name "$env:PACKAGE_NAME"
```

This step should hard fail if docs contradict the package summary or native
release route.

### Windows Native UI Capture Workflow

Add a step after screenshot capture:

```yaml
- name: Check documentation and UI evidence consistency
  shell: pwsh
  run: |
    python scripts/check-docs-artifact-consistency.py --mode ci
```

The capture workflow may not always have a package summary, so `ci` mode is
strict about doc contradictions but only warns about missing package evidence.

### Pull Request And Local Use

For local developers and docs-only changes:

```bash
python scripts/check-docs-artifact-consistency.py --mode local
```

Phase 5 may add a separate lightweight CI job if documentation churn becomes
frequent, but the release-blocking path remains the Windows native package
workflow.

## Implementation Constraints For Phase 5

- Use structured scanning with line numbers; do not rely on broad substring
  checks without reporting exact file and line.
- Keep the canonical facts in one table or constant map inside the script so the
  rule set is reviewable.
- Avoid network access. The checker only reads repository files and local
  artifact summaries.
- Make the script deterministic across Windows and Linux path separators.
- Prefer explicit allowlists for legacy/future sections over ad hoc exemptions.
- Add unit-style sample fixtures only if they are small and do not duplicate the
  whole documentation tree.

## Acceptance Criteria

- `scripts/check-docs-artifact-consistency.py` exists in Phase 5.
- It can run in `local`, `ci`, and `release` mode.
- It emits deterministic text output and optional JSON output.
- It hard fails on current release contradictions.
- It warns on labeled legacy/future/manual content according to the failure
  behavior table.
- The Windows native package workflow runs the checker with package summary
  input before artifact upload.

