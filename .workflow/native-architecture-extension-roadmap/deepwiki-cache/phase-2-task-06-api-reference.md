# API Reference - Phase 2 Task 06: Narrow Native Session Store Boundary

Generated: 2026-07-08T17:42:00+08:00

## Scope

Task 06 has no external dependency. It defines an internal storage port boundary around the current operations needed by the app while preserving the native file backend and Qt session store behavior.

## Internal Boundary Decision

The port should describe application-facing storage capabilities, not a concrete backend:

- open/schema lifecycle
- raw IO append/query/retention
- serial profile and UI preference persistence
- scan execution persistence/query
- match, rule, verification persistence/query
- last-error diagnostics

The port must not expose file names, cache internals, rewrite mechanics, SQLite assumptions, or commit/orphan recovery details. Those remain backend implementation concerns for later tasks.
