# API Reference - Phase 2 Task 07: Add File Commit and Orphan Recovery

Generated: 2026-07-08T18:05:00+08:00

## Scope

Task 07 has no external dependency. It improves internal native file storage recovery behavior while preserving the existing length-prefixed record format.

## Internal Recovery Decision

The native file backend uses existing artifacts as recovery signals:

- `.bak` means a replacement commit was not fully completed and should roll back to the backed-up file.
- `.tmp` is an uncommitted write artifact and should be removed during open recovery.
- A parse failure after a valid record prefix means the unreadable tail should be isolated to `.orphan` and the live file restored to its last complete record boundary.

The recovery path must preserve existing complete records, keep old record files readable, and avoid introducing SQLite or any new runtime dependency.
