# DeepWiki Phase 2 Research — UI Architecture Core

Generated: 2026-06-30T07:18:00+08:00

## Scope

Phase 2 introduces Win32 UI ownership boundaries for state, frame scheduling,
layout calculation, HWND transactions, and paint policy. Research was run before
Phase 2 Task 01.

## Repositories Queried

| Repository | Query |
|---|---|
| `microsoft/Windows-classic-samples` | Structure query and Win32 UI best practices for `WM_SIZE`, `WM_MOUSEMOVE`, child HWND layout, `SetWindowPos`, `MoveWindow`, `RedrawWindow`, `WM_SETREDRAW`, flicker control, live resize, and splitter drag. |
| `Kitware/CMake` | CMake practices for small C++ test executables, `add_executable`, `add_test`, `target_link_libraries`, and conditional Windows-only targets. |

## Win32 Findings

- `WM_SIZE` is the normal trigger for recalculating child control layout after a
  parent client size change.
- During live resize or splitter drag, every event should not independently
  force a full-window erase and synchronous repaint.
- `BeginDeferWindowPos`, `DeferWindowPos`, and `EndDeferWindowPos` are the
  preferred pattern when multiple child HWNDs are moved or resized together.
- `SetWindowPos` offers flag control (`SWP_NOZORDER`, `SWP_NOACTIVATE`,
  `SWP_SHOWWINDOW`, `SWP_NOMOVE`, `SWP_NOSIZE`, etc.) and is better than
  `MoveWindow` when repaint, activation, Z-order, or visibility behavior must be
  explicit.
- `MoveWindow` is simpler, but its repaint boolean must be controlled carefully;
  `TRUE` can cause immediate paint work for every child move.
- Handling `WM_ERASEBKGND` or otherwise preventing unnecessary background erase
  is an important flicker-control technique.
- `InvalidateRect` with erase disabled and deferred paint is generally safer for
  hot interaction than full synchronous redraw.
- `RDW_UPDATENOW`, erase-heavy `RedrawWindow`, and synchronous repaint should be
  reserved for settle, first-paint, or explicit final-state refresh paths.
- Splitter drag can use live latest-position state while dropping stale work,
  but must not quantize the actual drag position or make the interaction stepped.

## CMake Findings

- Small C++ test executables should use normal `add_executable` plus
  `target_link_libraries` for the code under test.
- Tests should be registered with `add_test(NAME ... COMMAND ...)` using stable
  unique names.
- Windows-only targets or compile definitions should remain behind `if(WIN32)`
  or the existing project option gates.
- Future Phase 2 tests should follow the repository's existing native test
  style and avoid adding a new test framework.

## Phase 2 Constraints

1. Event handlers should update state and schedule work; they should not become
   the long-term owner of layout, HWND mutation, and repaint policy.
2. Geometry calculation must become pure enough to test independently.
3. HWND mutation should be diffed and batched so intermediate invalid layouts do
   not become visible.
4. Repaint policy must distinguish hot interaction from settle/first paint.
5. All new targets/tests must follow existing CMake patterns.

