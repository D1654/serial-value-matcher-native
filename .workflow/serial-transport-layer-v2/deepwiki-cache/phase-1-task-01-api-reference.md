# Phase 1 Task 01 API Reference

## Query Decision

No external library, framework, or runtime API applies to this task. The
dependency table specifies C++20 standard-library value types and existing
internal modules only, so no DeepWiki or external API query was required.

## Internal Contract Guidance

- Model session state, request identity, generation, deadlines, error evidence,
  and operation results as small neutral value types. Keep zero/unassigned
  semantics explicit and avoid Win32, UI, storage, or localized-text types.
- Make pure state/category/name predicates `noexcept` where their implementation
  cannot fail. Their behavior should be deterministic and directly testable.
- Define lifecycle, byte-stream, and write-scheduler capabilities as narrow
  non-owning interfaces. The single session owner implements them; capability
  consumers must not own, expose, or coordinate the native handle.
- Do not recreate the broad `SerialTransport` facade. Results for operations
  that can outlive their caller must retain request identity and session
  generation so stale completions can be rejected.
- Prefer immutable snapshots and structured terminal results over shared mutable
  error text or backend-specific state.

## Confidence

High. The task has no external dependency ambiguity, and the required contract
boundaries are fully described by the approved domain knowledge and task plan.
