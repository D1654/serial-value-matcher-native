# Phase 4 Research: Boundary and Release Closure

Generated: 2026-07-20

## Research Record

| Repository | Structure | Focused query | Result |
|---|---|---|---|
| `Kitware/CMake` | Available, including CTest architecture and test infrastructure | Repository script gates registered with `add_test` | Timed out after 15 seconds; use the already verified Task 05 CMake/CTest reference and local CMake graph. |
| `wine-mirror/wine` | Available, including filesystem/I/O, process/thread, hardware abstraction, and test infrastructure | PTY/Wine evidence boundaries | Timed out after 15 seconds; use the reviewed Task 04/05 Wine evidence limits and current harness behavior. |
| `actions/runner` | Available, including job execution, step processing, and release infrastructure | Exit propagation, artifacts, and truthful CI boundaries | DeepWiki answer returned successfully. |

The phase research used short timeouts after two research agents were blocked on
network calls. No new dependency or API is required by Phase 4.

## Applicable Contracts

### CMake And CTest

- A repository architecture checker should be a normal test registered with
  `add_test`, so a nonzero script exit fails focused and complete CTest runs.
- The checker must use the repository's configured Python interpreter rather
  than introduce a new runtime or test framework.
- Complete release invocations must remain unfiltered and use
  `--no-tests=error`; a focused boundary run is supplementary evidence only.

### Wine And PTY

- Wine/PTTY proves the serial adapter contract through Wine's Unix serial
  translation. It is local release-candidate evidence, not physical Windows
  driver, PLC, USB-serial, or GitHub Windows-runner evidence.
- Final reporting must retain `Classification=local-only-release-candidate-evidence`
  and `CiExecutesPtyMatrix=no`.
- The reviewed seven-scenario harness remains the authoritative local matrix:
  normal, reopen, timeout, pending cancel, stress, active close, and stale
  generation.

### GitHub Actions Runner

- Native nonzero exit codes fail a step unless a workflow explicitly softens
  them. Captured or piped native results must preserve the original exit code.
- Uploaded artifacts are evidence objects; upload success and artifact digest
  do not replace explicit file-completeness assertions or the package SHA256
  sidecar.
- A Windows workflow must not claim that it executed a POSIX PTY matrix. The
  existing local-only boundary files are the truthful CI contract.

## Phase Implications

1. Task 01 documents implemented ownership and the upper-layer codec boundary;
   it adds no code, UI control, storage schema, or Gray-code transport behavior.
2. Task 02 uses the Python standard library and CTest to reject forbidden
   dependency directions without adding a framework or compatibility layer.
3. Task 03 records fresh local evidence and final Windows workflow evidence,
   while keeping physical hardware testing an explicit limitation.
