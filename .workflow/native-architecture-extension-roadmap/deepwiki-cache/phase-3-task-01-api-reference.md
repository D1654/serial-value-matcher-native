# API Reference — Phase 3 Task 01: Define Session Evidence Model
Generated: 2026-07-09T14:20:36+08:00

## Dependency Conclusion

The task Dependencies table declares no external libraries or APIs:

| Library | GitHub Repo | APIs Used | Usage in This Task |
|---------|-------------|-----------|-------------------|
| None | None | None | Internal evidence model only. |

DeepWiki task-level API research is not required for this task. No task-specific network lookup was performed.

## Implementation Notes

- Define a local structured session evidence model only.
- Preserve raw TX/RX evidence before UI formatting, filtering, or refresh logic.
- Associate each evidence event with session id, monotonic order, wall-clock timestamp, and source subsystem.
- Cover event types for raw TX, raw RX, user command, Modbus scan settings, match result, report metadata, and app version.
- Keep fields serialization-ready without introducing a storage/schema dependency in this task.
- Add focused tests for ordering, optional metadata, serialization-ready fields, and privacy-sensitive fields.
