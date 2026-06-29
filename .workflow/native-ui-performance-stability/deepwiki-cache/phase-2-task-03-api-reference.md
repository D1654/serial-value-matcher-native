# Phase 2 Task 03 API Reference

Generated: 2026-06-30T07:34:19+08:00

## Scope

Task: Native Layout Transaction

This cache records the task-level API research used before implementing the
Win32 child-window layout transaction boundary.

## Win32 API: Deferred Child Window Positioning

Repository queried: `microsoft/Windows-classic-samples`

APIs covered:

- `BeginDeferWindowPos`
- `DeferWindowPos`
- `EndDeferWindowPos`
- `SetWindowPos`
- `SWP_NOREDRAW`
- `SWP_NOZORDER`
- `SWP_NOOWNERZORDER`
- `SWP_NOACTIVATE`

Relevant implementation notes:

- `BeginDeferWindowPos(nNumWindows)` returns an `HDWP` handle or `NULL` on
  failure. The initial count is a capacity hint for the batch.
- `DeferWindowPos` appends one window placement to the batch and returns the
  updated `HDWP`; `NULL` means that operation was not recorded and the batch
  should fall back to direct positioning.
- `EndDeferWindowPos` applies the recorded placements and returns nonzero on
  success. A zero result does not identify which child failed, so fallback must
  be conservative.
- `SetWindowPos` returns nonzero on success and zero on failure. `GetLastError`
  can be used for detailed diagnostics when needed.
- `SWP_NOREDRAW` prevents automatic redraw after position or visibility
  changes; callers must invalidate/redraw later through an explicit paint
  policy.
- `SWP_NOZORDER` keeps current z-order and ignores `hWndInsertAfter`.
- `SWP_NOACTIVATE` prevents layout operations from stealing focus.
- The classic use case is batching many child control moves during resize to
  reduce intermediate paints and flicker.

Implementation decision:

- Use `BeginDeferWindowPos` for multi-control geometry batches.
- Use direct `SetWindowPos` fallback if begin/defer/end fails.
- Preserve `SWP_NOREDRAW` during live splitter dragging; existing caller paint
  paths remain responsible for invalidation until the Phase 2 paint policy
  task centralizes those rules.

## CMake: Native Test Target Wiring

Repository queried: `Kitware/CMake`

APIs covered:

- `add_executable`
- `target_include_directories`
- `set_target_properties`
- `target_link_libraries`
- `add_test`

Relevant implementation notes:

- `add_executable(<target> <sources>...)` creates the test binary from explicit
  source files.
- `target_include_directories(<target> PRIVATE src)` keeps test include paths
  target-local.
- `set_target_properties` can disable Qt automoc/uic/rcc for native-only tests.
- `add_test(NAME <name> COMMAND <target>)` registers the target for CTest;
  this requires top-level testing to already be enabled.
- Native Win32 tests in this repository belong inside the existing
  `if(SVM_BUILD_WIN32_APP)` block and must link required system libraries.

Implementation decision:

- Add `native_ui_layout_transaction_tests` next to the existing native layout
  tests.
- Link the transaction test with `user32`, `gdi32`, and `comctl32` because it
  creates real hidden Win32 child controls under Wine/Windows.
