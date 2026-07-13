# Task 04 API Reference — Deterministic Serial Session Contract Suite

Generated: 2026-07-13

## Dependency Decision

The task dependency table declares `None`. The implementation uses only the
Phase 1 internal C++20 contracts in `serial_session.h`, `serial_types.h`, and
`serial_write_queue.h`; there is no external library, framework, repository, or
API to query. DeepWiki is therefore not applicable and was not invoked.

## Applicable Internal Contracts

- `SerialSession` owns lifecycle, endpoint, generation, control lines, and
  access to the narrow byte-stream and scheduler capabilities.
- `SerialByteStream` returns typed synchronous read/write results carrying an
  operation descriptor, deadline state, byte count, endpoint, and error
  evidence.
- `SerialWriteScheduler` distinguishes admission from terminal completion and
  exposes cancellation, completion collection, and a counted queue snapshot.
- `SerialWriteQueue` already enforces count and byte budgets, counts active work,
  assigns FIFO request IDs, preserves explicit deadlines and generations, and
  rejects mismatched or duplicate active completions without releasing the
  reservation.

The contract suite should include `serial_session.h` and
`serial_write_queue.h`, but must remove the broad `serial_transport.h` include
and the `FakeSerialTransport` compatibility fake.

## Recommended Test Double Shape

Keep one test-local `FakeSerialSession` implementing `SerialSession`,
`SerialByteStream`, and `SerialWriteScheduler`. Compose a real
`SerialWriteQueue` inside it rather than duplicating queue accounting. Add only
test-control methods needed to make otherwise transient behavior observable:

- an injected `steady_clock::time_point now` for deterministic deadline checks;
- a state-history vector recording `Opening` and `Closing` transitions;
- a scripted next byte-operation outcome for success, partial failure, timeout,
  disconnected/native failure, and read failure;
- `activateNextWriteForTest()` and `deliverCompletionForTest(...)` hooks;
- a terminal key set keyed by `(generation, requestId)` and an evidence vector;
- an explicit non-open state setter or constructor parameter to exercise
  `Opening`, `Closing`, and `Faulted` rejection paths without threads or sleeps.

Use a private monotonic generation counter separately from the currently
published generation. A successful open increments the counter and publishes
the new nonzero generation. Close settles old work first, then publishes
`Closed` with generation zero and an empty endpoint; the next generation is not
published until the following open succeeds.

## Executable Contract Cases

### State and Generation

1. Initial snapshot is `Closed`, generation `0`, empty endpoint.
2. Successful open records `Closed -> Opening -> Open`; the returned operation
   and open snapshot carry generation `1`, endpoint, and submitted options.
3. Close records `Open -> Closing -> Closed`; the close result identifies the
   generation and endpoint being closed, while the final snapshot invalidates
   them with generation `0` and an empty endpoint.
4. A second successful open publishes generation `2`, never reuses generation
   `1`, and does not expose generation `2` before generation `1` is invalidated.
5. Byte writes, reads, queued writes, DTR, and RTS submitted in `Closed`,
   `Opening`, `Closing`, or `Faulted` are rejected with
   `RejectedClosed`/`SessionClosed`, an unassigned request ID, and no queue or
   evidence mutation.

### Typed Results and Evidence

Use fixed payloads, endpoints, generations, deadlines, and native codes so every
field can be asserted directly:

| Case | Status | Deadline status | Error category | Byte count | Native code |
|---|---|---|---|---:|---:|
| Full write | `Succeeded` | `Met` or `NotSet` | `None` | payload size | `0` |
| Short write | `Failed` | `Met` or `NotSet` | `IoFailure` | partial count | `0` |
| Read error | `Failed` | `Met` or `NotSet` | `IoFailure` | `0` | `0` |
| Native write/read error | `Failed` | `Met` or `NotSet` | `NativeFailure` | transferred count | fixed nonzero code |
| Expired operation | `Timeout` | `Expired` | `Timeout` | transferred count | `0` |
| Operation while closed | `RejectedClosed` | `Pending` or `NotSet` | `SessionClosed` | `0` | `0` |

For every assigned operation, assert the operation kind, request ID,
generation, exact deadline, endpoint, `result.byteCount == error.byteCount`, and
that localized message parsing is unnecessary. Successful results carry no
error category or native code. Deadline tests must advance the injected clock;
they must not sleep.

### Queue Admission and Snapshots

Configure the fake queue with small limits such as two requests and four bytes
to keep assertions compact:

1. Accepted writes receive monotonically increasing nonzero request IDs and the
   current session generation; admission status is `Accepted` and deadline
   status is `Pending` when a deadline is set.
2. The next request beyond either count or byte capacity is immediately
   `RejectedFull`/`QueueFull`, has no assigned request ID, and does not alter
   FIFO order, byte counts, or the next accepted request's payload.
3. Activating the FIFO head transfers pending accounting to active accounting;
   `countedCount()` and `countedBytes()` remain unchanged.
4. Active work remains inside both budgets, so admission stays rejected until
   the active request reaches one terminal result.
5. Snapshot assertions cover configured capacities, pending/active counts,
   pending/active bytes, totals, full/empty state, and next request ID.

### Cancellation, Close, and Exactly Once

Define exactly-once over all observable result channels: the vector returned by
`cancelPendingWrites()` plus results returned once by
`takeCompletedWrites()`. A result must never appear in both channels.

1. Cancelling pending writes returns FIFO terminal `Cancelled` results, releases
   each reservation once, and a repeated cancellation returns no terminal
   result.
2. Cancelling active work completes its matching active reservation exactly
   once; a duplicate completion is rejected and cannot add evidence or release
   later work.
3. Closing with pending and active writes settles every accepted request before
   publishing `Closed`; `takeCompletedWrites()` returns each close-settled result
   once, and its second call is empty.
4. Every terminal result preserves the accepted request ID, old generation,
   original deadline, endpoint, and actual byte count.
5. After all settlement, counted requests and bytes are zero and no work is
   replayed on reopen.

`SerialOperationStatus` currently has no distinct terminal `Closed` value. For
this task's test-only typed mapping, represent close settlement as
`Cancelled` with `SerialErrorCategory::SessionClosed`; direct submissions after
close remain `RejectedClosed` with the same category. This is the only
implementable neutral encoding without changing the Task 04 source-file scope.
The legacy queue's `SerialWriteResultStatus::Closed` may be mapped at the fake
boundary and must not leak into assertions against the neutral result type.

### Stale Completion Suppression

1. Open generation `1`, accept and activate a request, close, then open
   generation `2` and accept new work.
2. Deliver a synthetic completion tagged with generation `1` after generation
   `2` is open.
3. Return a test-hook rejection with `RejectedInvalid` and `InvalidInput`, but do
   not publish it through `takeCompletedWrites()` or the evidence vector.
4. Assert generation `2` session state, endpoint, queue counts/bytes, active
   identity, completion count, and evidence are byte-for-byte unchanged.
5. Deliver the matching generation `2` completion and assert it settles once;
   repeat it and assert the duplicate is likewise suppressed.

The stale check must compare both generation and request ID before mutating the
queue or evidence. Request ID equality alone is insufficient across reconnects.

## Target Registration

Rename the executable and CTest registration from `transport_contract_tests`
to `serial_session_contract_tests`. Keep it linked to `svm_slim_core` and the
`src` include directory. The test translation unit must have no Win32/UI header
dependency and no reference to `SerialTransport`, `SerialIoResult`, or
`FakeSerialTransport`.

## Implementation Risks

- `SerialSession` methods are synchronous, so `Opening` and `Closing` are only
  observable through fake state history or explicit test hooks in this phase.
- The neutral result type's missing distinct `Closed` terminal status is a
  contract gap; the documented `Cancelled + SessionClosed` pair avoids adding a
  compatibility abstraction inside a test-only task.
- Queue accounting is already covered exhaustively in Task 03. Task 04 should
  validate scheduler integration and lifecycle interaction, not copy every
  standalone queue test.
- Evidence suppression must happen before completion collection; filtering only
  when results are read would allow stale state mutation.

## Sources and Confidence

- `.workflow/serial-transport-layer-v2/context/domain-knowledge.md` — lifecycle,
  generation, exactly-once, close/reconnect, and evidence rules. Confidence:
  high.
- `.workflow/serial-transport-layer-v2/deepwiki-cache/phase-1-research.md` — no
  external dependency and neutral-contract guidance. Confidence: high.
- `src/transport/serial_session.h` and `src/transport/serial_types.h` — current
  narrow capability and typed-result declarations. Confidence: high.
- `src/transport/serial_write_queue.h/.cpp` and
  `tests/serial_write_queue_tests.cpp` — current dual-budget, FIFO, active, and
  completion behavior. Confidence: high.
- Task 04 plan — required file scope, cases, verification, and target rename.
  Confidence: high.

Overall confidence: high. No external API uncertainty affects this task.
