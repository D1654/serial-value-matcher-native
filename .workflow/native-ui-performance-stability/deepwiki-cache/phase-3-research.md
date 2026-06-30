# DeepWiki Cache - Phase 3: Functional Closure And Regression Gates

Generated: 2026-06-30T18:18:00+08:00

## Scope

Phase 3 proves existing functional behavior remains intact after the UI architecture changes. The phase-level dependencies are Win32 API for native serial/UI behavior and CMake/CTest for native regression gates.

## Sources

| Library | Repo / Source | Query Status | Notes |
|---|---|---|---|
| Win32 API | `microsoft/Windows-classic-samples` via DeepWiki | Partial | DeepWiki found serial usage in classic samples, especially `CreateFile`, `ReadFile`, `WriteFile`, overlapped I/O, worker-thread communication patterns, and `GetLastError` checks. It did not expose complete references for `SetCommState`, `SetCommTimeouts`, `DCB`, `COMMTIMEOUTS`, or `ClearCommError`. |
| Microsoft Learn | Official Win32 API docs | Fallback | Used for exact serial API semantics where DeepWiki coverage was incomplete or returned 500 on focused Task 01 queries. |
| CMake/CTest | `Kitware/CMake` via DeepWiki | Covered | DeepWiki confirmed `add_executable`, `add_test`, `enable_testing`, `ctest --test-dir`, `--output-on-failure`, `-R`, and `RUN_SERIAL` usage patterns. |

## Win32 Serial Guidance

- Open COM ports through the Win32 device path form, especially `\\.\COM10` and higher.
- Check every Win32 API result and translate `GetLastError()` into actionable status text.
- Configure serial state after open with a populated `DCB` and `SetCommState`.
- Validate known invalid `DCB` combinations before open where possible; this keeps predictable user configuration errors out of driver-specific failure paths.
- Configure `COMMTIMEOUTS` explicitly. Keep UI poll paths short and avoid long blocking reads on the UI thread.
- Use `ClearCommError`/`COMSTAT` before reads to detect queued input and communication errors.
- For high-throughput or long waits, prefer worker-thread/overlapped patterns; for this project, the current UI path keeps serial polling bounded with short timeout checks.

## CMake/CTest Guidance

- Register native regression executables with `add_executable` and `add_test`.
- Use `ctest --test-dir <build-dir> --output-on-failure -R <regex>` for focused gates.
- Keep PTY/Wine loopback as an explicit script gate because it needs a pseudo-terminal and Wine COM symlink setup beyond normal CTest discovery.

## Phase 3 Implication

Phase 3 should combine three evidence levels:

- pure native unit tests for validation, state, and error translation;
- PTY loopback for real Win32 serial open/write/read/close/reopen behavior under Wine;
- executable `--self-test` / `--ui-perf-test` to confirm UI architecture changes did not hide controls or regress responsiveness.
