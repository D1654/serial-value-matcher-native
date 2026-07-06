# Phase 1: UI / Architecture Foundation

> Parent: [Project Plan](../../project-plan.md)
> Status: pending

---

## Objective

Stabilize production UI/layout and `NativeMainWindow` boundaries so later backend and extension work can land without reintroducing tab blanks, resize flicker, hidden controls, or god-window growth.

## Prerequisites

- [ ] Phase 2 draft approved.
- [ ] Current v1.0.4 user-validated UI behavior treated as baseline.
- [ ] No TCP UI/runtime or broad feature expansion is introduced in this phase.

## Libraries & Dependencies

| Library | GitHub Repo | Used For |
|---------|-------------|----------|
| Win32 API / Windows samples | microsoft/Windows-classic-samples | Message handling, layout, repaint, DPI, native desktop patterns. |
| CMake | Kitware/CMake | Build/test target structure and CTest registration. |
| GitHub Actions artifact flow | actions/upload-artifact | UI screenshot/perf evidence preservation. |

## Task List

| # | Task | Description | Files | Est. Steps |
|---|------|-------------|-------|------------|
| 1 | Establish UI Baseline Gates | Capture current UI/perf/docs/package gates and make baseline expectations explicit. | `docs/测试与验证.md`, `docs/Windows原生UI验证.md`, `.github/workflows/windows-native-ui-capture.yml`, `scripts/capture-windows-native-ui-wine.sh` | 8 |
| 2 | Promote Layout Model Contract | Ensure production resize/tab layout is driven by `NativeLayoutModel` and tested for edge sizes. | `src/win32/native_layout_model.h`, `src/win32/native_layout_model.cpp`, `src/win32/main_window_layout.cpp`, `tests/native_ui_layout_model_tests.cpp`, `tests/native_layout_metrics_tests.cpp` | 10 |
| 3 | Harden Layout Transaction and Paint Policy | Reduce resize flicker through batched HWND movement, paint policy, and frame scheduling. | `src/win32/native_layout_transaction.h`, `src/win32/native_layout_transaction.cpp`, `src/win32/native_paint_policy.h`, `src/win32/native_paint_policy.cpp`, `src/win32/native_frame_scheduler.h`, `src/win32/native_frame_scheduler.cpp`, `src/win32/main_window_messages.cpp`, `tests/native_ui_layout_transaction_tests.cpp`, `tests/native_paint_policy_tests.cpp`, `tests/native_frame_scheduler_tests.cpp` | 12 |
| 4 | Stabilize Workbench Split and Tab State | Protect tab/log split behavior, current-page prompt visibility, and resizable panel state. | `src/win32/native_workbench_tab_state.h`, `src/win32/native_workbench_tab_state.cpp`, `src/win32/native_log_scroll_state.h`, `src/win32/native_log_scroll_state.cpp`, `src/win32/main_window_layout.cpp`, `tests/native_workbench_tab_state_tests.cpp`, `tests/native_log_scroll_state_tests.cpp` | 10 |
| 5 | Define Main Window Shell Seams | Make `NativeMainWindow` shell responsibilities explicit and prepare controller seams without broad moves. | `src/win32/native_main_window_context.h`, `src/win32/main_window.h`, `src/win32/main_window_messages.cpp`, `src/win32/main_window_commands.cpp`, `src/win32/main_window_lifecycle.cpp` | 9 |
| 6 | Extract Serial Send Log UI Boundaries | Move serial/send/log state coordination toward small controller-style helpers while preserving HWND ownership. | `src/win32/native_serial_send_controller.h`, `src/win32/native_serial_send_controller.cpp`, `src/win32/main_window_serial.cpp`, `src/win32/main_window_send.cpp`, `src/win32/main_window_log.cpp`, `src/win32/native_serial_io_state.h`, `src/win32/native_serial_io_state.cpp`, `src/win32/native_send_control_state.h`, `src/win32/native_send_control_state.cpp`, `src/win32/native_log_model.h`, `src/win32/native_log_model.cpp`, `tests/native_serial_io_state_tests.cpp`, `tests/native_send_control_state_tests.cpp`, `tests/native_log_filter_state_tests.cpp`, `tests/native_send_history_state_tests.cpp` | 14 |
| 7 | Extract Modbus Analysis Preference UI Boundaries | Clarify Modbus/analysis/preferences UI coordination before backend unification. | `src/win32/native_modbus_analysis_controller.h`, `src/win32/native_modbus_analysis_controller.cpp`, `src/win32/main_window_modbus.cpp`, `src/win32/main_window_analysis.cpp`, `src/win32/main_window_preferences.cpp`, `src/win32/native_modbus_scan_ui_state.h`, `src/win32/native_modbus_scan_ui_state.cpp`, `src/win32/native_ui_preferences.h`, `src/win32/native_ui_preferences.cpp`, `tests/native_modbus_scan_ui_state_tests.cpp`, `tests/native_analysis_report_tests.cpp`, `tests/native_ui_preferences_tests.cpp` | 13 |
| 8 | Phase 1 UI Regression Closure | Run and tighten native UI capture/perf/self-test evidence for all key tabs and resize cases. | `.github/workflows/windows-native-ui-capture.yml`, `scripts/capture-windows-native-ui.ps1`, `scripts/capture-windows-native-ui-wine.sh`, `docs/Windows原生UI验证.md` | 8 |

## Deliverables

- [ ] Production layout/model/transaction path is explicit and test-backed.
- [ ] `NativeMainWindow` shell seams are documented in code structure and plan evidence.
- [ ] Key tabs, current-page prompt, split resizing, window resizing, and UI perf have reproducible checks.
- [ ] No behavior changes outside UI/architecture foundation scope.

## Verification Checklist

- [ ] Local CTest passes.
- [ ] Native self-test passes.
- [ ] Native UI perf test passes against baseline-derived thresholds.
- [ ] UI screenshot/capture evidence covers single-send, multi-send, file, Modbus, analysis/workbench, resize, and prompt visibility.
- [ ] GitHub Actions UI capture artifact remains valid.

## Phase-Specific Risks

| Risk | Mitigation |
|------|------------|
| Controller extraction becomes cosmetic churn | Keep files adjacent unless a new directory is justified by behavior-protected extraction. |
| UI flicker persists despite layout work | Use `--ui-perf-test`, screenshot evidence, frame scheduling, and paint policy tests before closing phase. |
| Main window seams are unclear | Add narrow helper interfaces only where call sites and tests prove value. |

---

> Detailed task instructions are in the `tasks/` subdirectory.
