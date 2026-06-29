# API Reference — Phase 2 Task 02: Native Layout Model

Generated: 2026-06-30T07:24:05+08:00

| API | Library | Source | Confidence |
|---|---|---|---|
| `add_executable` | CMake | DeepWiki `Kitware/CMake` | High |
| `target_sources` | CMake | DeepWiki `Kitware/CMake` | High |
| `target_include_directories` | CMake | DeepWiki `Kitware/CMake` | High |
| `target_link_libraries` | CMake | DeepWiki `Kitware/CMake` | High |
| `set_target_properties` | CMake | DeepWiki `Kitware/CMake` | High |
| `add_test` | CMake | DeepWiki `Kitware/CMake` | High |
| `if(WIN32)` | CMake | DeepWiki `Kitware/CMake` | High |

## CMake Commands

### `add_executable`

Defines an executable target from a name and source list. The project already
uses this pattern for native unit-style test executables.

### `target_sources`

Adds sources to an existing target with `PRIVATE`, `PUBLIC`, or `INTERFACE`
scope. Relative paths are interpreted from `CMAKE_CURRENT_SOURCE_DIR`.

### `target_include_directories`

Adds include paths to a target with `PRIVATE`, `PUBLIC`, or `INTERFACE` scope.
Task 02 should keep using `target_include_directories(... PRIVATE src)` for
native tests that include `win32/...` headers.

### `target_link_libraries`

Links target dependencies and usage requirements. Task 02 should link the new
test target to any small native layout library or include the pure model sources
directly, matching existing project style.

### `set_target_properties`

Sets target properties such as C++ standard. Existing native tests use
`CXX_STANDARD 20` and `CXX_STANDARD_REQUIRED YES`; Task 02 should follow that.

### `add_test`

Registers an executable with CTest. If the command is an executable target, CMake
substitutes the built target path. Tests pass on exit code `0`.

### `if(WIN32)`

Use for Windows-only test targets and source files. The existing
`native_layout_metrics_tests` target is already behind `if(WIN32)`; the new
layout model test should stay in the same block.

## Task 02 CMake Constraints

- Do not introduce a new test framework.
- Use the existing native test style: `add_executable`, `set_target_properties`,
  `target_include_directories`, and `add_test`.
- Keep the target Windows-only if it includes Win32 layout headers guarded by
  `_WIN32`.
- Add only sources required for pure layout model tests.

