# Task 05: Strengthen CI Release Gates

> Phase: 3 — Production Hardening
> Status: Completed

---

## Objective

Register the hardened session sources/tests and require transport-v2 regression evidence in existing Windows, MinGW/Wine, package, size/security, artifact, and documentation gates without weakening current release constraints.

## Files

**Create:**
- None

**Modify:**
- `CMakeLists.txt`
- `.github/workflows/windows-native-package.yml`
- `.github/workflows/windows-native-ui-capture.yml`
- `scripts/package-windows-native-mingw.sh`
- `scripts/check-docs-artifact-consistency.py`

**Test:**
- `CMakeLists.txt`
- `.github/workflows/windows-native-package.yml`
- `.github/workflows/windows-native-ui-capture.yml`
- `scripts/package-windows-native-mingw.sh`
- `scripts/check-docs-artifact-consistency.py`

## Dependencies (**REQUIRED** — Task Research Agent reads this table)

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| CMake/CTest | `Kitware/CMake` | `add_library`, `add_executable`, `add_test`, `cmake --build`, `ctest --test-dir` | Register session implementation and focused regression targets in host, Windows, and MinGW configurations. |
| Windows API | `microsoft/Windows-classic-samples` | `CreateFileW`, `ReadFile`, `WriteFile`, `CancelIoEx`, `GetLastError` | Ensure CI compiles and executes the actual native session implementation, not only fake contracts. |
| Wine | `wine-mirror/wine` | `wine`, `wineboot`, `WINEPREFIX`, `xvfb-run` | Keep strict local self-test/UI performance and PTY release-candidate evidence available. |
| GitHub Actions | `actions/runner` | `actions/checkout`, `actions/upload-artifact`, PowerShell `$LASTEXITCODE`, `GITHUB_STEP_SUMMARY` | Preserve CI-blocking test, package, artifact completeness, and digest evidence. |
| 7-Zip | `ip7z/7zip` | `7z a -tzip` | Create the existing portable MinGW package without introducing a runtime dependency. |
| Python | `python/cpython` | `hashlib`, `pathlib`, package inspector entry point | Run package size/import/Unicode/forbidden-runtime/docs-set checks. |

## Steps

### Step 1: Register production sources

Add the final session implementation sources to the correct existing CMake target and keep transport-neutral code in `svm_slim_core` only when it has no Win32 dependency.

### Step 2: Register focused tests

Add or update CTest executables for queue, session, typed-error, reconnect, and PTY coverage without reintroducing Qt or a new framework.

### Step 3: Preserve build variants

Verify host native-only, Windows MSVC Release, and MinGW cross configurations all resolve the same source graph with the appropriate Win32 guards.

### Step 4: Keep cross-build caveat explicit

Retain the rule that `scripts/build-windows-native-mingw.sh` builds only `svm-native-win32`; require a separate full `cmake --build build-windows-native-mingw --parallel 1` before cross CTest.

### Step 5: Update package workflow tests

Make `.github/workflows/windows-native-package.yml` run the complete CTest result before self-test, UI performance, packaging, and artifact upload.

### Step 6: Extend backend evidence

Append transport-v2 queue, exactly-once, typed-error, generation, and PTY-fault coverage to the existing backend closure evidence without renaming the established `phase-2-backend-regression.txt` path.

### Step 7: Preserve PTY boundary evidence

Keep `serial-pty-matrix.txt` and `serial-pty-matrix-summary.txt` explicit that POSIX PTY scenarios are local-only while CTest/no-port safety remains CI-blocking.

### Step 8: Keep strict Wine behavior

Ensure `scripts/package-windows-native-mingw.sh` retains strict Wine/Xvfb defaults, writes the Wine gate status to the package summary, and never converts a failed strict gate into a pass.

### Step 9: Preserve package audit

Continue invoking the Python package inspector with the existing 5 MiB zip and 8 MiB extracted limits, SHA256 sidecar, import scan, Unicode probe, forbidden-runtime, unexpected-DLL, required-file, documentation-link, and documentation-file-set checks.

### Step 10: Update consistency rules

Add only stable transport-v2 evidence terms to `scripts/check-docs-artifact-consistency.py`; preserve existing package names, workflow paths, artifact names, and required documentation terms.

### Step 11: Assert artifact completeness

Require package zip, hash, summary, CTest log, self-test log, UI-performance log, backend closure, and PTY boundary/summary files before upload; retain `if-no-files-found: error` and explicit per-file assertions.

### Step 12: Check UI workflow trigger

Confirm `.github/workflows/windows-native-ui-capture.yml` continues to trigger for `src/transport/**`, `src/win32/**`, tests, CMake, and the PTY harness even though the transport change has no intended UI redesign.

### Step 13: Validate workflow text

Run the documentation checker against both workflow files and verify no active document claims a missing artifact, old executable, Qt route, or weakened gate.

### Step 14: Review release diff

Inspect CMake target inventory, workflow evidence names, package summary terms, and size/security gates together before committing; do not edit project source outside the Level-2 paths in this task.

## Verification

- [x] Host and Windows/MinGW CMake graphs register all hardened session sources/tests.
- [x] Package workflow remains CI-blocking for CTest, self-test, UI perf, package audit, docs consistency, and evidence completeness.
- [x] PTY remains local-only evidence and is not misreported as Windows CI execution.
- [x] Existing package size/security/hash/runtime/documentation gates remain unchanged or stricter.
- [x] UI workflow still triggers for transport source/test changes.

**Focused command:**
```bash
cmake -S . -B /tmp/svm-transport-v2-cmake -G Ninja -DSVM_BUILD_WIN32_APP=OFF
cmake --build /tmp/svm-transport-v2-cmake --parallel
ctest --test-dir /tmp/svm-transport-v2-cmake --output-on-failure -R "transport_contract|serial_write_queue|native_reconnect_state"
python3 scripts/check-docs-artifact-consistency.py
```

**Focused expected evidence:**
```
100% tests passed
docs consistency ok
```

**MinGW command:**
```bash
scripts/build-windows-native-mingw.sh
cmake --build build-windows-native-mingw --parallel 1
ctest --test-dir build-windows-native-mingw --output-on-failure
```

**MinGW expected evidence:**
```
100% tests passed
```

**PTY command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_SERIAL_LOOPBACK_SCENARIOS=normal,reopen,timeout,cancel,stress,close,reopen-generation-isolation \
SVM_SERIAL_LOOPBACK_SUMMARY=artifacts/local/task-05-serial-pty-matrix-summary.txt \
python3 scripts/run-windows-native-serial-pty-loopback.py
```

**PTY expected evidence:**
```
GateStatus=passed
Classification=local-only-release-candidate-evidence
Scenarios=normal,reopen,timeout,cancel,stress,close,reopen-generation-isolation
```

**Wine command:**
```bash
mkdir -p /tmp/svm-v2-task-05-xdg
chmod 700 /tmp/svm-v2-task-05-xdg
env WINEPREFIX=/tmp/svm-v2-task-05-wine WINEARCH=win64 XDG_RUNTIME_DIR=/tmp/svm-v2-task-05-xdg \
xvfb-run -a bash -c 'wine build-windows-native-mingw/svm-native-win32.exe --self-test && wine build-windows-native-mingw/svm-native-win32.exe --ui-perf-test'
```

**Wine expected evidence:**
```
ok
ui-perf ok
```

**Package command:**
```bash
SVM_MINGW_BUILD_DIR=build-windows-native-mingw \
SVM_MINGW_PACKAGE_DIR=artifacts/windows-native-mingw-task-05 \
SVM_WINEPREFIX=/tmp/svm-v2-task-05-wine \
SVM_STRICT_WINE_TEST=1 \
scripts/package-windows-native-mingw.sh
```

**Package expected evidence:**
```
Gate status: passed
Wine gate status: passed
Zip sha256 file matches: yes
Unexpected DLL files: none
Required package files: passed
Package documentation links: passed
Package documentation file set: passed
```

**CI command/reference:**
```text
Windows package: .github/workflows/windows-native-package.yml
Windows UI capture: .github/workflows/windows-native-ui-capture.yml
```

**CI expected evidence:**
```
native-ctest.log contains: 100% tests passed
native-self-test.log contains: ok
native-ui-perf-test.log contains: ui-perf ok
package summary contains: Gate status: passed
artifact upload completes with if-no-files-found: error
```

**Final consistency command:**
```bash
python3 scripts/check-docs-artifact-consistency.py
git diff --check
```

**Final consistency expected evidence:**
```
docs consistency ok
```

## Commit

```
ci: strengthen serial transport release gates (Phase 3, Task 05)
```
