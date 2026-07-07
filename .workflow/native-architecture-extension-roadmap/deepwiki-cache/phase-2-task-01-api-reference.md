# Task 01 API Reference — Define Serial Write Queue Contract

- Generated: 2026-07-07T16:04:57+08:00
- Task: Phase 2 / Task 01 — Define Serial Write Queue Contract

## Dependency Table Result

The task plan declares no external libraries:

| Library | GitHub Repo | APIs Used | Usage |
|---------|-------------|-----------|-------|
| None | None | None | Core queue contract uses standard C++ only. |

## Research Decision

No task-level external API query is required because the implementation must use standard C++ only. Phase-level DeepWiki research already covered:

- `microsoft/Windows-classic-samples` for Win32 async/cancellation/timeout design references.
- `qt/qtserialport` for behavioral write-buffer/error-state reference only.
- `Kitware/CMake` for CTest registration patterns.

## Implementation Constraints

- Do not include Qt headers in `serial_write_queue.*`.
- Do not include Win32 headers or `HWND` in `serial_write_queue.*`.
- Keep request/result types deterministic and value-based so they are easy to test.
- Prefer all-or-reject enqueue for backpressure; partial writes are an integration concern for Task 02.
- Represent outcomes distinctly:
  - accepted/enqueued
  - rejected/full/invalid
  - sent
  - failed
  - timeout
  - cancelled
- Native UI state may expose queue counters/status, but should not own write transport behavior.
