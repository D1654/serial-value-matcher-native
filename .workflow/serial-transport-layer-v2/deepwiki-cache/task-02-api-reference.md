# API Reference - Task 02: Enforce Transport Boundaries

Generated: 2026-07-20

## Research Record

The dedicated research query to `Kitware/CMake` did not return within the
bounded research window. This reference uses the committed Task 05 CMake/CTest
research, Phase 4 research, and the checked-in CMake graph as the required
fallback. No new library is introduced.

| API | Source | Confidence |
|---|---|---|
| `find_package(Python3 COMPONENTS Interpreter)` | CMake contract plus local configure validation | High |
| `add_test(NAME ... COMMAND ...)` | Task 05 DeepWiki reference plus local CTest graph | High |
| `ctest --test-dir` | Task 05 DeepWiki reference plus local execution | High |
| Python script exit semantics | CPython standard library and project convention | High |

## CMake And CTest

### Python Interpreter

Use:

```cmake
find_package(Python3 REQUIRED COMPONENTS Interpreter)
```

`Python3_EXECUTABLE` is the configured host interpreter. The repository already
uses Python in Windows workflows, MinGW/PTTY tools, package inspection, and
documentation checks, so this does not add a packaged runtime dependency.

### Test Registration

Use a normal CTest entry:

```cmake
add_test(
    NAME transport_boundary_tests
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-transport-boundaries.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}"
)
```

- `NAME` becomes the stable CTest test name.
- `COMMAND` receives separate arguments; paths remain quoted by CMake.
- Script exit `0` passes and any nonzero result fails the test.
- No `RUN_SERIAL`, skip expression, compatibility target, or special expected
  output property is justified.
- The complete release suite remains unfiltered and uses `--no-tests=error`.

## Boundary Checker Contract

### Repository Root

- Default to `Path(__file__).resolve().parents[1]`.
- Accept `--root` for CTest and deterministic self-check fixtures.
- Require `src/transport` to exist; missing scan roots are failures, not empty
  passes.

### Scan Scope

Scan all C/C++ source and header files under `src/transport`, not only today's
four neutral files. This catches newly added forbidden transport files while
allowing the existing RTU adapter's legitimate dependency on core Modbus
contracts.

Reject these categories from `src/transport`:

1. Win32/UI paths, namespaces, handles, and concrete serial APIs.
2. Matcher, codec, bit-layout, or Gray implementation symbols.
3. Native storage dependencies.
4. Removed facade symbols/files: `SerialTransport` and `Win32SerialPort`.

Each violation must include file, line, stable rule id, explanation, and a
short remediation hint.

### Self-Test

`--self-test` should use in-memory allowed and forbidden samples. It must prove
every rule category is rejected and that neutral serial types remain allowed.
It must not modify repository files or depend on temporary fixtures.

Expected success output:

```text
Boundary self-test: passed.
Transport boundary check: passed.
```

## Precision Notes

- Do not forbid the word `transport`; `SerialRtuTransport` is valid.
- Do not forbid all `src/core` references; the RTU adapter legitimately uses
  core Modbus exchange contracts.
- Do forbid analysis/matcher/codec dependencies in neutral transport.
- Match identifiers and include paths rather than arbitrary prose fragments
  where possible, so comments and ordinary words do not create noise.
- A missing source directory, unreadable file, or invalid root must fail closed.
