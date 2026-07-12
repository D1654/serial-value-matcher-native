# DeepWiki Phase Research — Phase 1: Contract Foundation

Generated: 2026-07-12T14:55:00+08:00

## Dependency Inventory

Phase 1 has no external libraries or frameworks. It uses C++20 standard-library
value types and existing internal project modules only.

## DeepWiki Decision

No repository/API query is applicable for this phase. The DeepWiki script is
present and can be invoked through `bash`; it is not executable directly. Task
research still runs for every task and must record that no external API lookup
was required.

## Phase Guidance

- Keep neutral contracts free of Win32 and UI headers.
- Keep lifecycle, byte, and write-scheduler capability declarations together in
  one `serial_session.h` file.
- Preserve command-sequence admission semantics and current RTU behavior.
- Use deterministic pure tests before changing the native backend.
