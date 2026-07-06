# Phase 3: Extension Capability & Production Hardening

> Parent: [Project Plan](../../project-plan.md)
> Status: pending

---

## Objective

Add evidence and command-sequence foundations while hardening release, diagnostics, metadata, documentation, and production gates around the Windows native artifact.

## Prerequisites

- [ ] Phase 1 and Phase 2 completed.
- [ ] UI, serial, Modbus, and storage foundations are stable.
- [ ] No TCP UI/runtime, SQLite backend, broad plugin system, or full script engine is introduced.

## Libraries & Dependencies

| Library | GitHub Repo | Used For |
|---------|-------------|----------|
| CMake | Kitware/CMake | Version metadata, build/package target governance, CTest. |
| GitHub Actions artifact flow | actions/upload-artifact | Release evidence preservation. |
| Win32 API / resource metadata | microsoft/Windows-classic-samples | VERSIONINFO, native metadata, local diagnostics reference patterns. |

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Define Session Evidence Model | Add structured evidence model for TX/RX, user action, scan parameters, match results, and version metadata. | `src/capture/session_evidence.h`, `src/capture/session_evidence.cpp`, `src/capture/capture_bus.h`, `src/capture/capture_bus.cpp`, `src/session/console_model.h`, `src/session/console_model.cpp`, `src/storage/session_store.h`, `tests/session_evidence_tests.cpp` | 13 |
| 2 | Add Evidence Export and Redaction | Export local diagnostic/evidence bundles with optional redaction and no upload behavior. | `src/report/evidence_bundle_writer.h`, `src/report/evidence_bundle_writer.cpp`, `src/report/rule_verification_report.h`, `src/report/rule_verification_report.cpp`, `src/native_storage/native_session_store.h`, `src/native_storage/native_session_store.cpp`, `docs/故障排查.md`, `docs/用户指南.md`, `tests/evidence_bundle_writer_tests.cpp`, `tests/rule_verification_report_tests.cpp` | 12 |
| 3 | Add Declarative Command Sequence Foundation | Add local command sequence model and assertions using existing serial queue/Modbus executor, not a script engine. | `src/command_sequence/command_sequence.h`, `src/command_sequence/command_sequence.cpp`, `src/transport/serial_write_queue.h`, `src/modbus/modbus_scan_executor.h`, `src/capture/session_evidence.h`, `CMakeLists.txt`, `tests/command_sequence_tests.cpp` | 14 |
| 4 | Add Dangerous Operation Confirmation and Audit | Gate dangerous writes/batch commands/broadcast writes and record user-confirmed audit evidence. | `src/core/dangerous_operation_policy.h`, `src/core/dangerous_operation_policy.cpp`, `src/win32/main_window_send.cpp`, `src/win32/main_window_modbus.cpp`, `src/capture/session_evidence.h`, `src/capture/session_evidence.cpp`, `CMakeLists.txt`, `tests/dangerous_operation_policy_tests.cpp` | 12 |
| 5 | Single-Source Version Metadata | Align CMake version, Win32 VERSIONINFO, package name, README/docs, release notes, and artifact summary. | `cmake/svm_version.cmake`, `CMakeLists.txt`, `src/win32/app.rc`, `scripts/inspect-windows-package.py`, `README.md`, `docs/发布产物.md`, `docs/Windows发布说明.md`, `tests/version_metadata_tests.cpp` | 12 |
| 6 | Harden Package Docs Dependency Gates | Strengthen package audit, docs consistency, forbidden runtime, and release evidence checks. | `scripts/inspect-windows-package.py`, `scripts/inspect-windows-package.ps1`, `scripts/check-docs-artifact-consistency.py`, `.github/workflows/windows-native-package.yml`, `docs/测试与验证.md`, `docs/发布产物.md` | 11 |
| 7 | Finalize Performance and Serial Evidence Gates | Turn baseline-derived UI perf and serial fake/local PTY evidence into explicit release-candidate gates. | `scripts/capture-windows-native-ui.ps1`, `scripts/capture-windows-native-ui-wine.sh`, `scripts/run-windows-native-serial-pty-loopback.py`, `.github/workflows/windows-native-ui-capture.yml`, `.github/workflows/windows-native-package.yml`, `docs/Windows原生UI验证.md`, `docs/Windows串口真机验收.md` | 10 |
| 8 | Release Runbooks and Final Documentation | Update Chinese-first runbooks for build, artifact verification, UI review, serial stress, release, rollback, and diagnostics. | `README.md`, `docs/开发者指南.md`, `docs/测试与验证.md`, `docs/发布产物.md`, `docs/Windows发布说明.md`, `docs/故障排查.md`, `docs/用户指南.md` | 10 |

## Deliverables

- [ ] Evidence/diagnostic export exists and remains local/redactable.
- [ ] Command sequence foundation exists without a general script engine.
- [ ] Dangerous writes and batch/broadcast operations require explicit confirmation and audit.
- [ ] Version metadata and package/docs/release evidence are consistent.
- [ ] Release runbooks define final GitHub Actions artifact verification.

## Verification Checklist

- [ ] Local CTest passes.
- [ ] GitHub Actions native package workflow passes.
- [ ] UI capture workflow passes.
- [ ] Package audit and docs consistency pass.
- [ ] Release-candidate runbook covers local PTY evidence if CI loopback remains unavailable.
- [ ] No TCP runtime/UI, SQLite backend, or heavy dependency enters release.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| Evidence package leaks private paths/data | Add redaction mode and document complete-vs-redacted export. |
| Command sequence becomes a scripting engine | Keep commands declarative and local, backed by existing queue/executor. |
| Release gates become too heavy | Formalize existing scripts first; add only missing checks. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
