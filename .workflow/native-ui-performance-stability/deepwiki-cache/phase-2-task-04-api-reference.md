# Phase 2 Task 04 API Reference

Generated: 2026-06-30T07:47:36+08:00

## Scope

Task: Native Frame Scheduler

This cache records the task-level API research used before introducing UI
frame coalescing for Win32 native hot paths.

## Win32 API: Posted UI Frame Messages

Repository queried: `microsoft/Windows-classic-samples`

APIs and messages covered:

- `PostMessage`
- custom `WM_APP`/application messages
- `WM_SIZE`
- `WM_MOUSEMOVE`

Relevant implementation notes:

- `PostMessage(hwnd, message, wParam, lParam)` places a message in the target
  window queue and returns immediately.
- It returns nonzero on success and `FALSE` on failure; `GetLastError` can be
  used for diagnostics if the caller needs to surface the failure.
- Frequent messages such as `WM_SIZE` and `WM_MOUSEMOVE` should not do heavy
  work directly when the app needs stable interaction performance.
- `WM_PAINT` can be coalesced by the system, but resize/mouse input should be
  explicitly coalesced by application state when only the latest target matters.
- The safe pattern for this task is:
  1. update latest desired UI state on each hot event,
  2. post one private frame message if no frame is already queued,
  3. process the latest state once on the UI thread,
  4. clear the posted flag only when that frame message is handled.

Implementation decision:

- Use a new `WM_APP`-based private message for native UI frames.
- Keep the scheduler pure/testable; main window code owns the actual
  `PostMessageW` call and HWND work.
- Preserve continuous splitter behavior by storing the exact latest requested
  workbench height, never quantizing or bucketing it.
- Keep explicit final settle redraw paths for mouse-up/double-click; this task
  coalesces live high-frequency work rather than weakening correctness redraws.

## CMake: Native Test Target Wiring

Repository queried: `Kitware/CMake`

APIs covered:

- `add_executable`
- `target_include_directories`
- `set_target_properties`
- `add_test`

Relevant implementation notes:

- Add native-only C++ test targets inside the existing `if(SVM_BUILD_WIN32_APP)`
  block.
- Disable Qt automoc/uic/rcc on these test executables.
- Keep include paths target-local with `target_include_directories(... PRIVATE
  src)`.
- Register the target with `add_test(NAME ... COMMAND ...)`.

Implementation decision:

- Add `native_frame_scheduler_tests` next to the layout model and transaction
  tests.
- The scheduler test is pure C++ and does not need Win32 system libraries.
