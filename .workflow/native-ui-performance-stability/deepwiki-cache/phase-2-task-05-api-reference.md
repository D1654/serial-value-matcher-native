# Phase 2 Task 05 API Reference

Generated: 2026-06-30T07:57:05+08:00

## Scope

Task: Paint Policy Integration

This cache records the task-level API research used before centralizing native
paint invalidation and redraw choices.

## Win32 API: Redraw And Invalidation Policy

Repository queried: `microsoft/Windows-classic-samples`

APIs, messages, and flags covered:

- `RedrawWindow`
- `InvalidateRect`
- `WM_SETREDRAW`
- `WM_ERASEBKGND`
- `RDW_INVALIDATE`
- `RDW_ERASE`
- `RDW_ALLCHILDREN`
- `RDW_NOERASE`
- `RDW_UPDATENOW`

Relevant implementation notes:

- `InvalidateRect(hwnd, rect, TRUE)` invalidates and requests background erase;
  `FALSE` avoids background erase.
- `RedrawWindow` gives explicit control over invalidation, erase behavior,
  child redraw inclusion, and immediate paint.
- `RDW_INVALIDATE` adds the target region to the update region.
- `RDW_ERASE` requests background erase before painting.
- `RDW_NOERASE` suppresses background erase and is useful in resize/drag hot
  paths to reduce flicker.
- `RDW_ALLCHILDREN` includes child windows in the redraw operation.
- `RDW_UPDATENOW` forces immediate `WM_PAINT` processing and should be avoided
  in high-frequency live interaction unless the path is a deliberate settle or
  first-paint fallback.
- `WM_SETREDRAW` can temporarily suppress redraw during grouped UI mutations;
  callers must re-enable redraw and invalidate explicitly afterward.
- Handling or avoiding `WM_ERASEBKGND` is a common flicker reduction strategy.

Implementation decision:

- Define named policy functions for:
  - live drag/resize no-erase region redraw,
  - settle/full refresh,
  - first show,
  - workbench tab repaint while dragging vs settled,
  - workbench background z-order flags,
  - log flush redraw,
  - simple no-erase/erase invalidation.
- Keep direct `RedrawWindow` calls only where the policy module itself owns the
  raw flags.
- Keep `RDW_UPDATENOW` out of live drag/resize policies.

## CMake: Native Test Target Wiring

Repository queried: `Kitware/CMake`

APIs covered:

- `add_executable`
- `target_include_directories`
- `set_target_properties`
- `add_test`

Relevant implementation notes:

- Add native-only C++ test targets inside `if(SVM_BUILD_WIN32_APP)`.
- Disable Qt automoc/uic/rcc for native-only test executables.
- Keep include paths target-local.
- Register the target with `add_test(NAME ... COMMAND ...)`.

Implementation decision:

- Add `native_paint_policy_tests` next to the existing native UI architecture
  tests.
- The policy tests validate generated flags and confirm hot policies do not
  include `RDW_UPDATENOW` or erase-heavy flags.
