# Phase 1 Task 03 API Reference

## Query Decision

No external library, framework, or runtime API applies to this task. The
dependency table specifies C++20 containers, clocks, and existing internal
queue types only, so no DeepWiki or external API query was required.

## Internal Queue Correctness Guidance

- Treat request-count capacity and byte capacity as one atomic admission
  decision. Count both pending and active work, reject an oversized request or
  a request that would exceed either limit, and leave all existing work
  unchanged on rejection.
- Check byte admission without unsigned overflow: compare the payload size to
  the configured limit and to the remaining capacity before adding it. Apply
  the equivalent checked rule to request-count accounting and reject invalid
  zero-capacity configuration deterministically.
- Reserve one request slot and exactly `payload.size()` bytes when admission
  succeeds. Moving a request from pending to active transfers that reservation
  between snapshot fields; it must not reduce total counted work.
- Store the accepted request's monotonic request ID, session generation,
  payload byte size, and explicit deadline with the request. Compute the
  deadline once from a monotonic clock and do not replace it with a port-wide
  timeout later.
- Keep request IDs strictly increasing and never reuse zero or a previously
  issued value. Detect exhaustion before incrementing or wrapping and reject
  further admission deterministically rather than producing an ambiguous ID.
- Release pending reservations exactly once when a request is cancelled or
  cleared, and emit exactly one typed terminal result for each released
  request. Repeated cancellation or clearing must not emit a second result or
  decrement count/bytes again.
- Release an active reservation exactly once on every terminal completion:
  sent, failed, timed out, disconnected, cancelled, or closed. Preserve the
  reported transferred-byte count for short or partial writes, but release the
  full reserved payload size from the queue budget.
- Ensure completion identity matches the active request before releasing its
  reservation. A stale, duplicate, or mismatched completion must not release
  another request's count or bytes.
- Make snapshots satisfy these invariants after every operation:
  `counted_count == pending_count + active_count`,
  `counted_bytes == pending_bytes + active_bytes`, and neither total exceeds
  its configured limit. `next_request_id` must describe the next admissible ID
  without mutating queue state.
- Preserve FIFO order across admission and activation. Capacity pressure must
  never block, reorder, silently drop, overwrite, merge, or split requests;
  rejection is immediate and typed.

## Focused Verification Guidance

- Cover count-full and byte-full rejection independently, including a single
  payload larger than 256 KiB and arithmetic-boundary cases.
- Prove that active work remains counted and that pending-to-active movement
  leaves total count and bytes unchanged.
- Exercise cancellation, clear, success, partial success, failure, timeout,
  disconnect, and close paths, checking exact release and exactly-once terminal
  results after each path.
- Verify monotonic IDs, FIFO activation, stored generation/deadline values,
  duplicate-completion resistance, and unchanged file-send admission and
  completion semantics.

## Confidence

High. The approved queue limits and lifecycle rules are explicit, there are no
external dependency semantics to resolve, and the accounting invariants are
deterministic and directly testable.
