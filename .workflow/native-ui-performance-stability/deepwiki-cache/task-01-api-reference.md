# API Reference — Task 01: Source And CI Inventory

Generated: 2026-06-30T02:40:36+08:00

| API | Library | Source | Confidence |
|---|---|---|---|
| `add_executable` | CMake | DeepWiki `Kitware/CMake`; phase cache | High |
| `add_library` | CMake | DeepWiki `Kitware/CMake`; phase cache | High |
| `add_custom_target` | CMake | DeepWiki `Kitware/CMake`; phase cache | High |
| `enable_testing` | CMake | DeepWiki `Kitware/CMake`; phase cache | High |
| `add_test` | CMake | DeepWiki `Kitware/CMake`; phase cache | High |
| `set_tests_properties` | CMake | DeepWiki `Kitware/CMake`; phase cache | High |
| `jobs.<job_id>` | GitHub Actions runner | DeepWiki `actions/runner`; workflow schema semantics | High |
| `steps[]` | GitHub Actions runner | DeepWiki `actions/runner`; runner step processing | High |
| `runs-on` | GitHub Actions runner | DeepWiki `actions/runner`; workflow schema | High |
| `env` | GitHub Actions runner | DeepWiki `actions/runner`; workflow schema | High |
| `strategy.matrix` | GitHub Actions runner | DeepWiki `actions/runner`; workflow examples | High |
| Artifact upload/download steps | GitHub Actions runner | DeepWiki `actions/runner`; workflow examples | Medium |
| Workflow gate audit signals | GitHub Actions runner | DeepWiki `actions/runner`; `if`, `needs`, step processing | High |

## CMake Target Signals

### `add_executable`

**Source:** DeepWiki query against `Kitware/CMake`.

**Signature/Pattern:**

```cmake
add_executable(<name> [WIN32] [MACOSX_BUNDLE] [EXCLUDE_FROM_ALL] <sources>...)
add_executable(<name> IMPORTED [GLOBAL])
add_executable(<name> ALIAS <target>)
```

**Parameters/Fields:** target name, optional platform flags, source list, imported/alias form.

**Returns/Effect:** Defines an executable build target. If referenced from `add_test(COMMAND <target>)`, CMake can substitute the built executable path.

**Errors/Gotchas:** Do not infer active release targets from filenames alone; inventory only declared targets. `WIN32` is important for native GUI executable classification.

**Task Usage:** Identify the Win32 native executable, native test executables, and Qt-era executable targets.

### `add_library`

**Source:** DeepWiki query against `Kitware/CMake`.

**Signature/Pattern:**

```cmake
add_library(<name> [STATIC | SHARED | MODULE] [EXCLUDE_FROM_ALL] <sources>...)
add_library(<name> OBJECT <sources>...)
add_library(<name> INTERFACE)
add_library(<name> IMPORTED ...)
add_library(<name> ALIAS <target>)
```

**Parameters/Fields:** target name, library kind, source list, imported/interface/alias form.

**Returns/Effect:** Defines a library target used by executables/tests or exported as part of the build graph.

**Errors/Gotchas:** Interface/imported/alias targets may not produce artifacts but still affect include paths, compile definitions, and linkage.

**Task Usage:** Classify native core, storage, serial, and Qt-era libraries.

### `add_custom_target`

**Source:** DeepWiki query against `Kitware/CMake`.

**Signature/Pattern:**

```cmake
add_custom_target(<name> [ALL]
  [COMMAND <command> [args...]]
  [DEPENDS <depends>...]
  [BYPRODUCTS <files>...]
  [WORKING_DIRECTORY <dir>]
  [COMMENT <text>]
  [VERBATIM]
  [USES_TERMINAL]
  [SOURCES <sources>...])
```

**Parameters/Fields:** target name, optional `ALL`, commands, dependencies, byproducts, working directory.

**Returns/Effect:** Defines a build target for scripts, packaging, generation, audits, or other non-binary work.

**Errors/Gotchas:** A custom target usually has no primary output and may run every time when built. Treat it separately from executable/library artifacts.

**Task Usage:** Inventory package targets and distinguish Qt packaging from Win32 native script-driven packaging.

## CTest Signals

### `enable_testing`

**Source:** DeepWiki query against `Kitware/CMake`.

**Signature/Pattern:**

```cmake
enable_testing()
include(CTest)
```

**Parameters/Fields:** none.

**Returns/Effect:** Enables CTest test generation for the current directory scope and below.

**Errors/Gotchas:** `add_test` registrations only matter for CTest inventory when testing is enabled.

**Task Usage:** Determine whether native tests are actually registered for CI/test execution or merely present as source files.

### `add_test`

**Source:** DeepWiki query against `Kitware/CMake`.

**Signature/Pattern:**

```cmake
add_test(NAME <name>
  COMMAND <command> [args...]
  [CONFIGURATIONS <configs>...]
  [WORKING_DIRECTORY <dir>]
  [COMMAND_EXPAND_LISTS])
```

**Parameters/Fields:** test name, command/target, arguments, configurations, working directory.

**Returns/Effect:** Registers a test runnable by `ctest`.

**Errors/Gotchas:** A test command may reference an executable target, not a literal path. Inventory should map test name to target/component and not assume every `tests/*.cpp` file is registered.

**Task Usage:** Map native CTest entries to protected components and separate active native tests from Qt-only tests.

### `set_tests_properties`

**Source:** DeepWiki query against `Kitware/CMake`.

**Signature/Pattern:**

```cmake
set_tests_properties(<test>... PROPERTIES <property> <value> ...)
```

**Parameters/Fields:** test names and property/value pairs such as `LABELS`, `TIMEOUT`, `ENVIRONMENT`, `RUN_SERIAL`, `WILL_FAIL`.

**Returns/Effect:** Adds CTest metadata that changes scheduling, pass/fail interpretation, timeout, environment, and labels.

**Errors/Gotchas:** Test properties can make a test non-obvious as a gate, for example expected failure, regex-based pass/fail, serial execution, or long timeout.

**Task Usage:** Capture stress labels and any future native UI/serial labels.

## GitHub Actions Signals

### `jobs.<job_id>`

**Source:** DeepWiki query against `actions/runner`.

**Pattern:**

```yaml
jobs:
  <job_id>:
    name: <string>
    needs: <job_id | [job_id]>
    if: <expression>
    runs-on: <runner>
    strategy: <strategy>
    env: <mapping>
    steps: <sequence>
```

**Fields:** `needs`, `if`, `strategy`, `name`, `runs-on`, `timeout-minutes`, `continue-on-error`, `env`, `outputs`, `defaults`, `steps`.

**Effect:** Defines an executable CI job and its dependency/gating relationship.

**Gotchas:** Workflow filename does not prove active behavior. Gates depend on job graph, conditions, runner OS, and step exit behavior.

**Task Usage:** Classify workflows as active Win32 native, UI capture, Qt historical/parallel, or stress.

### `steps[]`

**Source:** DeepWiki query against `actions/runner`.

**Pattern:**

```yaml
steps:
  - name: <string>
    run: <command>
    shell: <shell>
  - name: <string>
    uses: <owner/repo@ref>
    with: <mapping>
```

**Fields:** run-step fields `run`, `shell`, `working-directory`; action-step fields `uses`, `with`; shared fields `name`, `id`, `if`, `timeout-minutes`, `continue-on-error`, `env`.

**Effect:** Executes shell commands or actions inside a job.

**Gotchas:** `continue-on-error: true` weakens a gate. Artifact upload does not prove validation unless earlier failing commands are mandatory.

**Task Usage:** Identify build, test, self-test, UI perf, package, capture, and artifact evidence steps.

### `runs-on`, `env`, `strategy.matrix`, artifact steps

**Source:** DeepWiki query against `actions/runner`.

**Patterns:**

```yaml
runs-on: windows-2022
env:
  BUILD_DIR: build-windows-native
strategy:
  matrix:
    os: [windows-latest]
- uses: actions/upload-artifact@<version>
  with:
    name: <artifact-name>
    path: <path>
```

**Effect:** Selects runner, supplies job/step variables, expands matrices, and persists artifacts.

**Gotchas:** Windows-native gates require Windows runners or explicit Wine/cross-build evidence. Artifact presence is evidence, not validation. Record exact action refs pinned in the repo.

**Task Usage:** Map executable package, hashes, screenshots, logs, and package summaries to producing workflows and scripts.

