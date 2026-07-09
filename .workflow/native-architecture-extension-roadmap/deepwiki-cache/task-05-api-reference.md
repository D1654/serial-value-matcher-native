# API Reference — Task 05: Single-Source Version Metadata
Generated: 2026-07-09T19:20:00+08:00

## Scope

Task: Phase 3 Task 05, "Single-Source Version Metadata".

Research target:
- CMake / `Kitware/CMake`: project version, configure variables, resource generation.
- Win32 VERSIONINFO / `microsoft/Windows-classic-samples`: native executable file/product metadata resource pattern.

No source files were modified by this research task. This file replaces an older, unrelated Task 05 cache entry.

## Cache Coverage

The phase-level cache `.workflow/native-architecture-extension-roadmap/deepwiki-cache/phase-3-research.md` exists and partially covers this task:
- It maps CMake/CTest to `Kitware/CMake`.
- It maps Win32 resource metadata to `microsoft/Windows-classic-samples`.
- It already concludes that `project(... VERSION ...)` and Win32 `VERSIONINFO` are the right lightweight mechanisms for Phase 3 production hardening.

It does not cover the task-level details needed for implementation: exact CMake variables, `configure_file` expansion behavior, Win32 numeric/string version syntax, language/codepage pairs, generated resource/header patterns, or package/docs audit pitfalls. Therefore, additional DeepWiki Task-level queries were run.

## Sources

DeepWiki queries run with `/root/.codex/skills/workflow-architect/assets/scripts/deepwiki.sh`:
- `Kitware/CMake`: project version, `PROJECT_VERSION*`, `configure_file`, generated resource/config files, pitfalls.
- `microsoft/Windows-classic-samples`: VERSIONINFO resource pattern, `FILEVERSION`, `PRODUCTVERSION`, `StringFileInfo`, `VarFileInfo`, translation pairs, pitfalls.
- Cross-repo query: CMake project metadata connected to Windows VERSIONINFO and package naming.

Primary docs checked for exact syntax:
- CMake `project`: https://cmake.org/cmake/help/latest/command/project.html
- CMake `configure_file`: https://cmake.org/cmake/help/latest/command/configure_file.html
- CMake `CPack`: https://cmake.org/cmake/help/latest/module/CPack.html
- Microsoft `VERSIONINFO`: https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource
- Microsoft `StringFileInfo`: https://learn.microsoft.com/en-us/windows/win32/menurc/stringfileinfo-block
- Microsoft `VarFileInfo`: https://learn.microsoft.com/en-us/windows/win32/menurc/varfileinfo-block

## CMake API And Variables

### `project(... VERSION ...)`

Syntax:

```cmake
project(SerialValueMatcherNative
    VERSION 1.0.4
    DESCRIPTION "Chinese-native Windows desktop serial debugging and value analysis tool"
    LANGUAGES CXX
)
```

Behavior:
- `VERSION <major>[.<minor>[.<patch>[.<tweak>]]]` accepts non-negative integer components.
- It sets:
  - `PROJECT_VERSION`
  - `PROJECT_VERSION_MAJOR`
  - `PROJECT_VERSION_MINOR`
  - `PROJECT_VERSION_PATCH`
  - `PROJECT_VERSION_TWEAK`
- It also sets project-prefixed variables:
  - `SerialValueMatcherNative_VERSION`
  - `SerialValueMatcherNative_VERSION_MAJOR`
  - `SerialValueMatcherNative_VERSION_MINOR`
  - `SerialValueMatcherNative_VERSION_PATCH`
  - `SerialValueMatcherNative_VERSION_TWEAK`
- From the top-level `CMakeLists.txt`, CMake also sets:
  - `CMAKE_PROJECT_VERSION`
  - `CMAKE_PROJECT_VERSION_MAJOR`
  - `CMAKE_PROJECT_VERSION_MINOR`
  - `CMAKE_PROJECT_VERSION_PATCH`
  - `CMAKE_PROJECT_VERSION_TWEAK`
  - `CMAKE_PROJECT_DESCRIPTION`, if `DESCRIPTION` is provided.

Important constraints:
- The top-level `CMakeLists.txt` must contain a direct `project()` call near the top, after `cmake_minimum_required()`.
- `project(VERSION ...)` is a good root source for build metadata, but external scripts cannot evaluate arbitrary CMake logic cheaply. Keep `cmake/svm_version.cmake` deliberately simple if Python/PowerShell/Bash package scripts will parse it.

Recommended local shape:

```cmake
# cmake/svm_version.cmake
set(SVM_VERSION "1.0.4")
set(SVM_VERSION_MAJOR "1")
set(SVM_VERSION_MINOR "0")
set(SVM_VERSION_PATCH "4")
set(SVM_VERSION_TWEAK "0")
set(SVM_RELEASE_TAG "v${SVM_VERSION}")
set(SVM_PRODUCT_DISPLAY_NAME "串口值匹配器")
set(SVM_PRODUCT_REPOSITORY_NAME "SerialValueMatcher Native")
set(SVM_PACKAGE_ARTIFACT "SerialValueMatcherNative-win32-native-x64")
set(SVM_WIN32_EXE_NAME "svm-native-win32.exe")
```

Then include it before the direct `project()` call and pass `VERSION "${SVM_VERSION}"`.

### `configure_file(...)`

Syntax:

```cmake
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/svm_version_resource.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/svm_version_resource.h"
    @ONLY
)
```

Expansion behavior:
- `@VAR@` references are replaced with the current CMake variable value.
- Without `@ONLY`, CMake also replaces `${VAR}`, `$CACHE{VAR}`, and `$ENV{VAR}` patterns. For `.rc`, headers, scripts, or docs templates, `@ONLY` avoids accidental substitutions.
- Undefined variables expand to an empty string.
- If the input file changes, the build system re-runs CMake and regenerates the output.
- The output timestamp is updated only when generated content changes.

Useful generated definitions for VERSIONINFO:

```c
#define SVM_VERSION_FILE_VERSION 1,0,4,0
#define SVM_VERSION_PRODUCT_VERSION 1,0,4,0
#define SVM_VERSION_FILE_VERSION_STR "1.0.4\0"
#define SVM_VERSION_PRODUCT_VERSION_STR "1.0.4\0"
#define SVM_VERSION_PRODUCT_NAME_STR "串口值匹配器\0"
#define SVM_VERSION_INTERNAL_NAME_STR "svm-native-win32\0"
#define SVM_VERSION_ORIGINAL_FILENAME_STR "svm-native-win32.exe\0"
```

Resource generation options:
- Generate a small header and include it from `src/win32/app.rc`. This is robust for MinGW `windres` and MSVC because quoted strings are not passed through command-line `/D` definitions.
- Or generate the whole `.rc` from `.rc.in` into the build directory and add the generated `.rc` to the `svm-native-win32` target.

For this repository, the generated-header approach is likely lower risk because `src/win32/app.rc` already exists and the task plan lists it as a modified file, not a newly generated source file.

### CPack Variables

If this project later moves native packaging into CPack:
- `CPACK_PACKAGE_NAME` defaults to the project name.
- `CPACK_PACKAGE_VERSION_MAJOR`, `_MINOR`, `_PATCH` default from `CMAKE_PROJECT_VERSION_MAJOR`, `_MINOR`, `_PATCH` when the top-level `project()` has version details.
- `CPACK_PACKAGE_VERSION` is built from those version components by default.
- `CPACK_PACKAGE_FILE_NAME` defaults to `${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}`.

The current repository uses custom PowerShell/Bash/Python package scripts rather than CPack, so Task 05 should explicitly wire the same version source into those scripts or their audits.

## Win32 VERSIONINFO Resource Syntax

Pattern:

```rc
#pragma code_page(65001)
#include <windows.h>
#include "resource.h"
#include "svm_version_resource.h"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION SVM_VERSION_FILE_VERSION
 PRODUCTVERSION SVM_VERSION_PRODUCT_VERSION
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0x0L
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "080404B0"
        BEGIN
            VALUE "CompanyName", SVM_VERSION_COMPANY_NAME_STR
            VALUE "FileDescription", SVM_VERSION_FILE_DESCRIPTION_STR
            VALUE "FileVersion", SVM_VERSION_FILE_VERSION_STR
            VALUE "InternalName", SVM_VERSION_INTERNAL_NAME_STR
            VALUE "OriginalFilename", SVM_VERSION_ORIGINAL_FILENAME_STR
            VALUE "ProductName", SVM_VERSION_PRODUCT_NAME_STR
            VALUE "ProductVersion", SVM_VERSION_PRODUCT_VERSION_STR
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0804, 1200
    END
END
```

Behavior:
- `versionID` for VERSIONINFO must be `1`; `VS_VERSION_INFO` is the conventional macro for that.
- `FILEVERSION` and `PRODUCTVERSION` are binary versions encoded as four 16-bit integers. Example `1,0,4,0`.
- The string values under `StringFileInfo` are user/diagnostic display strings. `FileVersion` and `ProductVersion` may be string versions such as `1.0.4`; keep the release tag prefix `v` out of the Windows version string unless intentionally using a marketing string.
- `StringFileInfo` `BLOCK "080404B0"` is language `0x0804` (Simplified Chinese) plus charset `0x04B0` (1200 decimal, Unicode).
- `VarFileInfo` must use the matching numeric pair: `VALUE "Translation", 0x0804, 1200`.
- `FILETYPE VFT_APP` is correct for `svm-native-win32.exe`.
- If `FILEFLAGS` includes `VS_FF_PRIVATEBUILD` or `VS_FF_SPECIALBUILD`, Microsoft requires corresponding `PrivateBuild` or `SpecialBuild` strings. Current release builds should normally keep `FILEFLAGS 0x0L` unless debug/prerelease metadata is deliberately represented.

## Common Pitfalls

- Current repository drift: `CMakeLists.txt` has `VERSION 1.0.0`, `src/win32/app.rc` embeds `1.0.0`, while `README.md` and release docs describe `v1.0.4`.
- Numeric Windows versions are comma-separated integers; string display versions are quoted strings. Do not feed `v1.0.4`, `1.0.4-beta`, or other non-numeric text into `FILEVERSION` / `PRODUCTVERSION`.
- Each binary version component is 16-bit. Validate or constrain CMake version components to `0..65535` before generating `FILEVERSION`.
- `PROJECT_VERSION_TWEAK` may be empty when the project version has only three components. Generate an explicit fourth resource component, usually `0`.
- `configure_file` expands undefined variables to empty strings. Missing version variables can silently produce broken `.rc` metadata unless tests parse the generated header/resource or final executable.
- Avoid passing quoted version strings to RC compilers through command-line definitions. A configured header is more portable across MSVC RC and MinGW `windres`.
- Do not compile two VERSIONINFO resources into the same executable. If a generated `.rc` is added, remove or stop compiling the hard-coded `src/win32/app.rc` VERSIONINFO block.
- Keep language/codepage block and `Translation` pair aligned. For this Chinese-native app, `080404B0` plus `0x0804, 1200` is coherent; `040904E4` plus `0x0409, 1252` is the usual U.S. English ANSI sample pattern.
- Package artifact names are currently hardcoded in `.github/workflows/windows-native-package.yml`, `scripts/package-windows-native.ps1`, `scripts/package-windows-native-mingw.sh`, `scripts/check-docs-artifact-consistency.py`, and docs. Any versioned artifact naming change will fan out unless it is also single-sourced.
- The package inspector currently reports zip/exe hashes, imports, forbidden runtime files, Unicode probes, and doc links, but it does not report Windows VERSIONINFO values. Task 05 should extend it to read and print `FileVersion`, `ProductVersion`, `ProductName`, and `OriginalFilename`, then fail on drift.

## Repository-Specific Recommendations

1. Make `cmake/svm_version.cmake` the only editable source for version/product/package constants.
   - Use simple `set(NAME "value")` assignments so CMake can include it and Python/PowerShell/Bash can parse it.
   - Define both bare version `1.0.4` and release tag `v1.0.4`; Windows binary metadata should use the bare version, release/docs links can use the tag.

2. Include `cmake/svm_version.cmake` before `project()` in `CMakeLists.txt`.
   - Keep the top-level direct `project(SerialValueMatcherNative VERSION "${SVM_VERSION}" ...)` call.
   - Move current product strings (`SVM_PRODUCT_DISPLAY_NAME`, `SVM_PRODUCT_REPOSITORY_NAME`, author, description) into the version metadata file or derive them there.

3. Generate resource metadata at configure time.
   - Preferred: configure `svm_version_resource.h` into `${CMAKE_CURRENT_BINARY_DIR}/generated` and include it from `src/win32/app.rc`.
   - Add the generated include directory to `svm-native-win32` so the RC compiler sees it.
   - Replace hard-coded `1,0,0,0` and `"1.0.0"` in `app.rc` with generated macros.

4. Extend package audit.
   - Python path: `pefile` can inspect PE version resources when available; the script already depends on `pefile` for imports.
   - PowerShell path: `Get-Item $exePath` exposes `.VersionInfo`.
   - Summary should include at least: `Native exe file version`, `Native exe product version`, `Native exe product name`, `Native exe original filename`.
   - Gate failures should compare those fields to `cmake/svm_version.cmake`.

5. Extend docs/artifact consistency checks.
   - Add version/release-tag terms to `scripts/check-docs-artifact-consistency.py`.
   - Treat active docs mentioning old release tags as failures unless the line is explicitly historical/contextual.
   - Keep package artifact base name stable unless the implementation intentionally chooses versioned package names; if versioned names are introduced, derive them from `SVM_PACKAGE_ARTIFACT` and `SVM_VERSION`.

6. Add focused tests.
   - `tests/version_metadata_tests.cpp` can parse `cmake/svm_version.cmake` and generated header/resource template inputs to verify that CMake metadata, resource macros, executable/package constants, and doc strings agree.
   - Package-level verification should remain in scripts because it needs the built Windows executable.

7. Fix the current version decision before coding.
   - The repository's public docs say `v1.0.4`, while build/resource metadata says `1.0.0`.
   - Task 05 should pick the intended current release version once, most likely `1.0.4`, then regenerate all derived forms from that.
