# API Reference - Phase 3 Task 03: Add Declarative Command Sequence Foundation

Generated: 2026-07-09T17:33:00+08:00

## Scope

Task 03 adds a local declarative command sequence and assertion foundation without introducing a general script engine.

Required dependency table for this task lists only:

| Library | GitHub Repo | APIs Used | Usage |
|---------|-------------|-----------|-------|
| CMake | Kitware/CMake | `add_executable`, `add_test` | Register command-sequence tests. |

This cache therefore focuses on CMake/CTest test-target registration only. It does not recommend any script engine, interpreter, dynamic expression runtime, or new third-party command-language dependency.

## Inputs Read

- `.workflow/native-architecture-extension-roadmap/context/domain-knowledge.md`
- `.workflow/native-architecture-extension-roadmap/phases/phase-3/tasks/task-03-add-declarative-command-sequence-foundation.md`
- `.workflow/native-architecture-extension-roadmap/deepwiki-cache/phase-3-research.md`
- Project `CMakeLists.txt` test registration area, read-only.

## DeepWiki Status

DeepWiki query succeeded after running the provided script through `bash`.

- Direct execution of `/root/.codex/skills/workflow-architect/assets/scripts/deepwiki.sh` failed with `Permission denied` because the script file is not executable.
- First sandboxed `bash` run failed with curl exit code `6`, consistent with restricted network/DNS access.
- Retried with approved network escalation and succeeded.

Query target:

- Repo: `Kitware/CMake`
- Question: C++20 CMake/CTest use of `add_executable` and `add_test` for registering a new unit test executable, including parameters, generated build/test effects, common patterns, and common mistakes.

DeepWiki search reference:

- `https://deepwiki.com/search/for-a-c20-cmakectest-project-e_e928a121-e304-402a-b18a-f0db1be9b380`

## CMake API Notes

### `add_executable`

Relevant signature:

```cmake
add_executable(<target> [source1 [source2 ...]])
```

Key semantics:

- Creates a logical executable target in the CMake build graph.
- The first argument is the target name. It should be unique in the project.
- The following arguments are source files compiled and linked into that executable.
- Test executables normally link to the project library under test with `target_link_libraries`.
- Include paths should be target-scoped with `target_include_directories`.
- Additional per-target properties can be applied after target creation.

Effect:

- CMake generates build rules to compile the listed sources and link the executable.
- The target can be referenced by later CMake commands, including `target_link_libraries`, `target_include_directories`, and `add_test`.

Task 03 implication:

- Add the new test source as a dedicated executable, likely named `command_sequence_tests`, using `tests/command_sequence_tests.cpp`.
- Link against the library that contains the command sequence implementation and its dependencies. If the implementation is added to `svm_slim_core`, link the test to `svm_slim_core`. If it needs Qt-only APIs, it would need the existing Qt test helper path, but Task 03 should prefer the non-Qt slim/native pattern when feasible.
- If the project still has Qt automoc context active, preserve existing non-Qt test properties:

```cmake
set_target_properties(command_sequence_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
```

### `add_test`

Recommended signature:

```cmake
add_test(NAME <test-name> COMMAND <command> [args...])
```

Key semantics:

- Registers a CTest test case.
- Requires `enable_testing()` somewhere in the configured directory tree. This project already calls `enable_testing()` before test target definitions.
- `NAME` is the CTest-visible test name used by `ctest -R`, reports, and failure output.
- `COMMAND` is the executable and arguments to run.
- When the command names a CMake executable target, CMake resolves it to the built executable path for the generated test file.
- A zero exit code passes; a non-zero exit code fails unless explicit pass/fail regex properties alter behavior.
- By default, the working directory is the current binary directory for the CMake directory that registered the test.

Optional parameters relevant when needed:

- `WORKING_DIRECTORY <dir>`: use if tests require fixture files or stable relative paths.
- `CONFIGURATIONS <config>...`: use only if the test is valid for selected build configurations.
- `COMMAND_EXPAND_LISTS`: useful when command arguments contain list-valued generator expressions.

Task 03 implication:

- Register the focused test with a CTest name that matches the target:

```cmake
add_test(NAME command_sequence_tests COMMAND command_sequence_tests)
```

- This makes the task verification regex work:

```bash
ctest --test-dir build-codex --output-on-failure -R "command_sequence|serial_write_queue|modbus_scan_executor"
```

## Existing Project Pattern

Current `CMakeLists.txt` already follows this CTest pattern:

```cmake
enable_testing()

add_executable(serial_write_queue_tests
    tests/serial_write_queue_tests.cpp
)
set_target_properties(serial_write_queue_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(serial_write_queue_tests PRIVATE svm_slim_core)
target_include_directories(serial_write_queue_tests PRIVATE src)
add_test(NAME serial_write_queue_tests COMMAND serial_write_queue_tests)
```

Qt tests use a helper:

```cmake
function(add_svm_qt_test target source)
    qt_add_executable(${target}
        ${source}
    )
    target_link_libraries(${target}
        PRIVATE
            svm_native_core
            Qt6::Test
    )
    add_test(NAME ${target} COMMAND ${target})
    if(ARGN)
        set_tests_properties(${target} PROPERTIES LABELS "${ARGN}")
    endif()
endfunction()
```

For Task 03, the lower-risk registration pattern is the non-Qt executable style used by `serial_write_queue_tests`, because the task objective is a local declarative model and existing backend integration, not a Qt behavior test.

## Recommended Task 03 CMake Shape

If `src/command_sequence/command_sequence.cpp` is added to `svm_slim_core`, the test registration should follow the existing slim-core test pattern:

```cmake
add_executable(command_sequence_tests
    tests/command_sequence_tests.cpp
)
set_target_properties(command_sequence_tests PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
target_link_libraries(command_sequence_tests PRIVATE svm_slim_core)
target_include_directories(command_sequence_tests PRIVATE src)
add_test(NAME command_sequence_tests COMMAND command_sequence_tests)
```

If the command sequence implementation cannot live in `svm_slim_core`, prefer a narrow project library target over compiling the same production `.cpp` directly into multiple test executables. Directly listing production sources in a test executable is acceptable for very small state-only Win32 tests in this repository, but a command-sequence foundation is likely shared behavior and should have a stable library boundary.

## Common Errors To Avoid

- Forgetting `add_test`: the executable builds, but `ctest -R command_sequence` finds nothing.
- Forgetting `enable_testing`: CTest metadata is not generated. This is already present in the project.
- Using the old `add_test(<name> <command> ...)` signature: prefer `NAME ... COMMAND ...` for modern behavior and future generator-expression compatibility.
- Hardcoding executable paths: use the target name in `COMMAND` so CMake resolves the built binary correctly.
- Mismatched test/target names: if the target or CTest name does not contain `command_sequence`, the documented verification regex may miss it.
- Missing `target_include_directories(... PRIVATE src)`: tests including project headers by `command_sequence/...` or `transport/...` may fail to compile.
- Linking the wrong project library: command-sequence tests should link the library containing `command_sequence.cpp` and existing backend abstractions, not duplicate a separate send path.
- Accidental Qt test registration for non-Qt logic: avoid `qt_add_executable` unless the test actually needs Qt event loop, signals, slots, or `Qt6::Test`.
- Cross-compiling execution mismatch: this project already sets `CMAKE_CROSSCOMPILING_EMULATOR` to Wine when cross-compiling for Windows and Wine is found. Tests intended to run under CTest during cross-builds should remain ordinary executable targets so the emulator mechanism can apply.
- Multi-config generators: CTest may require `ctest -C <Config>` under Visual Studio or similar generators. The documented `ctest --test-dir build-codex` flow likely targets a single-config local build.

## Declarative Command Sequence Boundary Notes

Although DeepWiki research for this task is CMake-only, the task context creates several implementation constraints that affect test registration and API shape:

- The command sequence should be a typed local data model, not parsed arbitrary code.
- Supported command kinds should be explicit: serial write, delay, wait-for-response, Modbus read, assertion.
- Safety limits should be ordinary data validation: maximum sequence length, timeout policy, cancellation state, and unsafe command rejection.
- Integration tests should exercise existing serial write queue and Modbus executor abstractions; they should not introduce a second serial send path.
- Evidence capture should record command execution and assertion outcomes as structured data.
- Tests should cover valid sequence execution, timeout, cancellation, assertion failure, and unsafe command rejection.

## Bottom Line

Task 03 needs no new CMake dependency and no scripting runtime. The project can register `command_sequence_tests` with the same `add_executable` plus `add_test(NAME ... COMMAND ...)` pattern already used by `serial_write_queue_tests`, preserving CTest discoverability and the documented focused verification command.
