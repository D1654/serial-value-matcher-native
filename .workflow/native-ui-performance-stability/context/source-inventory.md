# Source And CI Inventory

Generated: 2026-06-30T02:40:36+08:00

## Source-Of-Truth Basis

This inventory is based on current repository files, not README or stale documentation.

- Repository HEAD: `c4c8b272bf824ef027d5f042dfd736d09aa6231c`
- Primary files inspected: `CMakeLists.txt`, `.github/workflows/*.yml`, `src/**`, `tests/**`, `scripts/**`
- DeepWiki cache:
  - `.workflow/native-ui-performance-stability/deepwiki-cache/phase-1-research.md`
  - `.workflow/native-ui-performance-stability/deepwiki-cache/task-01-api-reference.md`

## Classification Legend

| Status | Meaning |
|---|---|
| `current-win32-native` | Active path for the requested release target and final GitHub Actions executable. |
| `current-supporting` | Current code or tests that support Win32 native behavior but are not the UI executable itself. |
| `parallel-qt` | Existing Qt path still present in build/CI, but not the authoritative final UI release target for this stabilization workflow. |
| `historical-or-legacy` | Old/historical route retained for comparison, packaging baseline, or future cleanup. |
| `unknown` | Present in the repository but not yet proven as current release behavior. |

## Build Options And Targets

| Item | Classification | Evidence | Notes |
|---|---|---|---|
| `SVM_BUILD_WIN32_APP` | `current-win32-native` | `CMakeLists.txt` option, default `OFF`; enabled by native workflows | Must be `ON` for `svm-native-win32`. It is Windows-only and errors on non-Windows. |
| `SVM_BUILD_QT_APP` | `parallel-qt` | `CMakeLists.txt` option, default `ON`; Qt package workflow enables it | Not the final release target for this stabilization workflow. |
| `SVM_BUILD_QT_TESTS` | `parallel-qt` | `CMakeLists.txt` option, default `ON` | Keeps Qt-era regression tests available. |
| `SVM_ENABLE_STRESS_TESTS` | `parallel-qt` | `CMakeLists.txt` option, default `OFF`; Qt stress workflows enable it | Stress path currently targets Qt test helper. |
| `svm-native-win32` | `current-win32-native` | `add_executable(... WIN32 ...)` under `if(SVM_BUILD_WIN32_APP)` | Primary Windows native executable. |
| `svm_slim_core` | `current-supporting` | CMake library from `src/core/*` | Protocol, Modbus, analysis, report core used by native executable/tests. |
| `svm_win32_serial` | `current-win32-native` | CMake library from Win32 serial sources under `if(WIN32)` | Serial port/enumerator implementation for native path. |
| `svm_native_storage` | `current-supporting` | CMake library from `src/native_storage/*` | File-backed native storage used by native executable/tests. |
| `svm_native_core` | `parallel-qt` | CMake library under `if(SVM_BUILD_QT_APP OR SVM_BUILD_QT_TESTS)` and links Qt | Qt-era shared core target, not the final native release target. |
| `svm-native` | `parallel-qt` | `qt_add_executable(... WIN32 ...)` under `if(SVM_BUILD_QT_APP)` | Qt executable. Keep separate from `svm-native-win32`. |
| `package-windows` | `historical-or-legacy` | CMake custom target invokes `scripts/package-windows.ps1` and depends on `svm-native` | Qt package custom target. Native packaging is script/workflow driven. |

## GitHub Actions Workflows

| Workflow | Classification | Runner | Enforced Gates | Artifacts |
|---|---|---|---|---|
| `.github/workflows/windows-native-package.yml` | `current-win32-native` | `windows-2022` | Configure native-only, build Release, `ctest`, `--self-test`, `--ui-perf-test`, native package script, package summary | `SerialValueMatcherNative-win32-native-x64.zip`, `.sha256.txt`, `.package-summary.txt` |
| `.github/workflows/windows-native-ui-capture.yml` | `current-win32-native` | `windows-2022` | Configure native-only, build Release, `ctest`, `--self-test`, `--ui-perf-test`, screenshot capture script | `windows-native-ui-screenshots` with PNGs, `capture-status.txt`, `ui-perf-test.log`, `window-info.txt` |
| `.github/workflows/windows-qt-package.yml` | `parallel-qt` | `windows-2022` | Install Qt, configure Qt app, build, `ctest`, Qt package script | `SerialValueMatcherNative-win-x64.zip`, `.sha256.txt`, `.package-summary.txt` |
| `.github/workflows/linux-qt.yml` | `parallel-qt` | `ubuntu-latest` | Install Qt packages, check Qt env, configure, build, `ctest` | None declared |
| `.github/workflows/windows-qt-stress.yml` | `parallel-qt` | `windows-2022` | Install Qt, configure `SVM_ENABLE_STRESS_TESTS=ON`, build `quality_stress_tests`, run `ctest -L stress` | None declared |
| `.github/workflows/linux-qt-stress.yml` | `parallel-qt` | `ubuntu-latest` | Install Qt packages, configure `SVM_ENABLE_STRESS_TESTS=ON`, build `quality_stress_tests`, run `ctest -L stress` | None declared |

### Workflow Findings

- The Windows native package workflow is the authoritative package build gate for this stabilization project.
- The Windows native UI capture workflow is the current automated visual smoke path.
- Qt workflows remain present and active in CI, but they are not evidence that the Win32 native release UI is correct.
- Current native workflows pin `actions/checkout` and `actions/upload-artifact` by commit with comments naming the action versions.

## Source Directory Inventory

| Path | Files | Classification | Role |
|---|---:|---|---|
| `src/win32` | 78 | `current-win32-native` | Win32 application entry point, main window, controls, layout, serial, send, log, Modbus, analysis, self-test, resources, UTF-8/UTF-16 helpers. |
| `src/core` | 10 | `current-supporting` | Small dependency-free core for protocol, Modbus, analysis, reporting, and text helpers. |
| `src/native_storage` | 11 | `current-supporting` | File-backed native storage, record codec, record I/O, session store, cache. |
| `src/modbus` | 14 | `current-supporting` | Modbus RTU request/response/codec/transport/scan planning and execution. Shared with Qt-era tests. |
| `src/matching` | 14 | `current-supporting` | Candidate generation, numeric decoding, scan observation, stability analysis, protocol rule verification. |
| `src/report` | 4 | `current-supporting` | Rule verification report and text file writer. |
| `src/app` | 9 | `parallel-qt` | Qt app entry, main window, Modbus worker/adapters, Qt serial byte channel. |
| `src/analysis` | 2 | `parallel-qt` | Qt-era stability analysis workflow target source. |
| `src/capture` | 4 | `parallel-qt` | Qt-era capture bus and raw I/O event types. |
| `src/protocol` | 4 | `parallel-qt` | Qt-era protocol checksum/payload codec sources used by Qt tests. |
| `src/session` | 2 | `parallel-qt` | Qt-era console model. |
| `src/storage` | 13 | `parallel-qt` | Qt-era SQLite/session/persistence records. |
| `src/transport` | 11 | `parallel-qt` | Qt-era serial service, selection, reconnect, enumerator, error translator. |

## Win32 Native Module Map

| Component | Representative Files | Protected By |
|---|---|---|
| App entry/self-test | `src/win32/main.cpp`, `src/win32/main_window_self_test.cpp` | Native workflows run `--self-test` and `--ui-perf-test`. |
| Main window lifecycle/layout | `main_window*.cpp`, `native_layout_metrics.*`, `native_control_utils.*` | `native_layout_metrics_tests`, `--ui-perf-test`, UI capture workflow. |
| Workbench tabs/status/progress | `main_window_workbench.cpp`, `main_window_status.cpp`, `native_workbench_tab_state.*`, `native_status_counters_state.*`, `native_progress_control.*` | `native_workbench_tab_state_tests`, `native_status_counters_state_tests`, UI perf/capture. |
| Serial connection/I/O | `main_window_serial*.cpp`, `win32_serial_*`, `native_serial_io_state.*`, `native_reconnect_state.*`, `native_connection_ui_state.*` | `native_win32_serial_tests`, `native_win32_serial_loopback_tests`, serial PTY script. |
| Send workflows | `main_window_send.cpp`, `native_send_codec.*`, `native_send_control_state.*`, `native_send_history_state.*`, `native_file_send_state.*` | `native_send_*_tests`, `native_file_send_state_tests`. |
| Logs | `main_window_log.cpp`, `native_log_model.*`, `native_log_scroll_state.*`, `native_log_view.*` | `native_log_filter_state_tests`, `native_log_scroll_state_tests`, UI perf log gate. |
| Modbus scan | `main_window_modbus.cpp`, `native_modbus_scan_*` | `native_protocol_modbus_tests`, `native_modbus_scan_*_tests`. |
| Candidate analysis/report | `main_window_analysis.cpp`, `native_analysis_workflow.*`, `src/core/analysis_core.*`, `src/report/*` | `native_win32_analysis_workflow_tests`, `native_analysis_report_tests`. |
| Preferences/storage | `main_window_preferences.cpp`, `main_window_storage.cpp`, `native_ui_preferences.*`, `src/native_storage/*` | `native_ui_preferences_tests`, `native_storage_tests`. |

## CTest Inventory

### Native / Native-Supporting Test Targets

These are declared with `add_executable` plus `add_test` and do not require the Qt helper function:

- `native_protocol_modbus_tests`
- `native_analysis_report_tests`
- `native_candidate_cache_state_tests`
- `native_connection_ui_state_tests`
- `native_modbus_scan_ui_state_tests`
- `native_modbus_scan_request_tests`
- `native_serial_io_state_tests`
- `native_file_send_state_tests`
- `native_serial_profile_codec_tests`
- `native_ui_preferences_tests`
- `native_workbench_tab_state_tests`
- `native_log_filter_state_tests`
- `native_log_scroll_state_tests`
- `native_reconnect_state_tests`
- `native_send_codec_tests`
- `native_send_control_state_tests`
- `native_send_history_state_tests`
- `native_status_counters_state_tests`
- `native_win32_serial_tests`
- `native_win32_serial_loopback_tests` (`WIN32` only)
- `native_storage_tests`
- `native_layout_metrics_tests` (`SVM_BUILD_WIN32_APP` only)
- `native_win32_analysis_workflow_tests` (`SVM_BUILD_WIN32_APP` only)

### Qt Helper Test Targets

These are declared through `add_svm_qt_test(...)` and link `svm_native_core` + `Qt6::Test`. They are useful regression coverage but not proof of the Win32 native UI release:

- `modbus_rtu_codec_tests`
- `modbus_read_request_tests`
- `modbus_read_response_tests`
- `modbus_scan_plan_tests`
- `modbus_scan_executor_tests`
- `modbus_scan_persistence_tests`
- `modbus_rtu_serial_transport_tests`
- `numeric_decoder_tests`
- `value_candidate_generator_tests`
- `match_persistence_tests`
- `candidate_stability_analyzer_tests`
- `stability_persistence_tests`
- `stability_analysis_workflow_tests`
- `protocol_rule_persistence_tests`
- `protocol_rule_verifier_tests`
- `protocol_rule_interpretation_tests`
- `protocol_rule_metadata_tests`
- `rule_verification_persistence_tests`
- `rule_verification_report_tests`
- `checksum_tests`
- `session_store_tests`
- `console_model_tests`
- `payload_codec_tests`
- `send_history_tests`
- `serial_profile_tests`
- `serial_port_selection_tests`
- `serial_reconnect_policy_tests`
- `serial_error_translator_tests`
- `quality_regression_tests`
- `quality_stress_tests` (`SVM_ENABLE_STRESS_TESTS` only, label `stress`)

## Script Inventory

| Script | Classification | Role |
|---|---|---|
| `scripts/package-windows-native.ps1` | `current-win32-native` | Windows native MSVC package script; finds `svm-native-win32.exe`, optionally runs self-test/UI perf, stages docs, calls PowerShell package inspector. |
| `scripts/inspect-windows-package.ps1` | `current-win32-native` | PowerShell package audit: file count/size, `svm-native-win32.exe`, Unicode text probe, forbidden Qt/SQLite runtime files. |
| `scripts/inspect-windows-package.py` | `current-win32-native` | Python package audit used by MinGW path; also checks forbidden Qt/SQLite/.NET runtime imports where available. |
| `scripts/capture-windows-native-ui.ps1` | `current-win32-native` | Windows native UI capture and UI perf log generation. |
| `scripts/run-windows-native-serial-pty-loopback.py` | `current-win32-native` | Local Wine/PTY serial loopback harness for `native_win32_serial_loopback_tests.exe`. |
| `scripts/build-windows-native-mingw.sh` | `current-supporting` | MinGW native build helper; configures Qt off and Win32 native on. |
| `scripts/package-windows-native-mingw.sh` | `current-supporting` | Local MinGW/Wine package path with optional Wine/Xvfb self-test and UI perf. |
| `scripts/capture-windows-native-ui-wine.sh` | `current-supporting` | Wine/Xvfb UI capture smoke path. Useful locally, not authoritative versus Windows Actions. |
| `scripts/package-windows.ps1` | `parallel-qt` | Qt portable package script using `windeployqt`; not current native release package. |
| `scripts/check-env.sh` | `parallel-qt` | Linux Qt dependency checker for CMake/Ninja/Qt6/SQLite plugin. |

## Executable-Level Native Gates

| Gate | Evidence |
|---|---|
| Native test suite | `ctest --test-dir <build> --output-on-failure -C Release` in both Windows native workflows. |
| Native self-test | `svm-native-win32.exe --self-test` via workflows and package/capture scripts. |
| Native UI perf | `svm-native-win32.exe --ui-perf-test`, with trace controlled by `SVM_NATIVE_SELF_TEST_LOG`. |
| UI screenshot capture | `scripts/capture-windows-native-ui.ps1`, uploaded as `windows-native-ui-screenshots`. |
| Package audit | `scripts/package-windows-native.ps1` plus `scripts/inspect-windows-package.ps1`; MinGW uses Python inspector. |
| Serial PTY loopback | `scripts/run-windows-native-serial-pty-loopback.py` and `native_win32_serial_loopback_tests` exist, but current Windows native package workflow does not yet run the PTY Python harness. |

## Recent Commit Signal

Recent commits are concentrated on native UI splitter, resize, repaint, and serial loopback hardening:

- `c4c8b27 fix: reduce native drag redraw flicker`
- `c2a6774 fix: reduce native splitter drag flicker`
- `cb8013f fix: preserve native child repaints on resize`
- `14f086a fix: stabilize native splitter dragging`
- `24753f1 fix: widen native log splitter hit area`
- `cd89e39 fix: flatten native current page hint panel`
- `24f598d feat: add constrained native log splitter`
- `6460f7e test: add serial loopback stress controls`

This supports the Phase 0 hypothesis that current risk is concentrated in Win32 native UI hot paths and validation gates.

## Unknowns For Follow-Up Tasks

- Latest GitHub Actions artifact run id, downloaded local artifact path, and package summary are not established in this task; they belong to Task 03 artifact baseline capture.
- Existing README and docs are not classified here; they belong to Task 02 documentation claim audit.
- The exact final documentation IA and consistency checker behavior are not defined here; they belong to Tasks 04 and 05.
- Serial PTY edge coverage exists locally but is not yet an enforced Windows Actions package gate.

## Task 01 Conclusion

The active final delivery path is the Win32 native executable `svm-native-win32` built by `.github/workflows/windows-native-package.yml` and visually smoked by `.github/workflows/windows-native-ui-capture.yml`. Qt workflows and Qt source/test paths remain present and active as parallel/historical coverage, but they must not be used as evidence that the Win32 native release UI is correct.

