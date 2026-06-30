# API Reference - Phase 4 Task 05: Package And Security Audit Hardening

Generated: 2026-06-30T21:17:13+08:00

## Sources

| API Area | Source | Confidence |
|---|---|---|
| GitHub Actions failure handling | Local workflow source | High |
| Windows native package audit | `scripts/inspect-windows-package.py`, `scripts/inspect-windows-package.ps1` | High |
| PE import table checks | Local Python `pefile` dependency and PowerShell binary parsing | Medium |

## Package Gate Notes

- Package scripts already generate a zip, a SHA256 sidecar, and a package summary.
- The package audit must fail closed for missing package files, missing `svm-native-win32.exe`, missing SHA256 sidecar, forbidden Qt/SQLite/.NET runtime files, forbidden imports, invalid Unicode probe, and size overflow.
- Python inspection can use `pefile` locally; missing `pefile` should be treated as a blocking audit failure because forbidden imports otherwise become unverifiable.
- PowerShell inspection needs equivalent release evidence in the summary because the Windows GitHub Actions package workflow uses the PowerShell path.

## Workflow Notes

- `scripts/package-windows-native.ps1` should remain the package workflow hard gate.
- The workflow should still explicitly assert the package summary exists and contains `Gate status: passed` so an accidental script regression cannot upload an unaudited artifact.
