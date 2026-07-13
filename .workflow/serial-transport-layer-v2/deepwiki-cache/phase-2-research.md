# DeepWiki Phase Research — Phase 2: Win32 Session and Caller Migration

Generated: 2026-07-13T10:37:08+08:00

## Dependency Inventory and Repository Mapping

| Dependency | GitHub Repository | Phase Usage | Research Status |
|---|---|---|---|
| Windows API (platform) | `microsoft/Windows-classic-samples` | COM `HANDLE` lifecycle, DCB/COMMTIMEOUTS, byte I/O, events/threads, cancellation, line control, close/reopen, and native error evidence | `structure` and one broad `ask` completed successfully |
| C++20 standard library | None | Value types, clocks, atomics, containers, optional/variant, and ownership restrictions | No external repository query required |

The phase plans consistently map the Windows platform APIs to
`microsoft/Windows-classic-samples`; no additional external library is used in
Phase 2.

## Queries Performed

### Structure

Command:

```text
bash /root/.codex/skills/workflow-architect/assets/scripts/deepwiki.sh structure microsoft/Windows-classic-samples
```

Result: success. The generated documentation tree is broad and groups relevant
material mainly under Device and Hardware APIs and Win32 Fundamentals. It does
not expose a dedicated serial-communications topic, so repository-wide examples
must be treated as supporting patterns rather than a complete COM-port guide.

### Broad Phase Ask

Command intent: identify patterns and pitfalls for a production Win32 serial
session owner across `HANDLE` ownership, DCB/COMMTIMEOUTS, synchronous
`ReadFile`/`WriteFile`, events and workers, `CancelIoEx`, settlement,
close/reopen, generation invalidation, DTR/RTS, native error evidence, and the
six planned migration tasks.

Result: success without retry or fallback. DeepWiki found strong generic Win32
examples for overlapped I/O, `OVERLAPPED` event ownership,
`WaitForSingleObject`/`WaitForMultipleObjects`, `GetOverlappedResult`, worker
threads, event signaling, `GetLastError`, and handle cleanup. It found only
limited serial-specific evidence. DCB, COMMTIMEOUTS, `PurgeComm`,
`ClearCommError`, `EscapeCommFunction`, COM unplug/replug behavior, and
`CancelIoEx` details were not directly demonstrated in the returned sample
snippets.

DeepWiki also surfaced a synchronous-cancellation sample based on
`CancelSynchronousIo`. That pattern is not interchangeable with `CancelIoEx`:
the former targets synchronous I/O issued by a particular thread, while the
latter belongs to the handle/operation cancellation model and still requires
the operation to reach a terminal completion.

## Applicable Findings

- A single object should own the COM `HANDLE`, its configuration, worker/event
  handles, buffers, and shutdown ordering. No UI or protocol caller should
  receive or close the native handle.
- Every event and worker handle needs one explicit owner and one cleanup path.
  Worker exit and I/O settlement precede destruction of their events, buffers,
  and the device handle.
- Successful or pending I/O must preserve the actual transferred-byte count;
  callers cannot assume a requested write or read completed in full.
- Event-based I/O must keep its `OVERLAPPED`, event, and backing buffer alive
  until completion or cancellation has been observed and settled. A cancellation
  request is not itself terminal completion.
- Waiting with a timeout is an application deadline signal, not proof that the
  kernel operation has ceased. The owner must cancel where supported and then
  collect the final outcome before reuse or close.
- Native numeric evidence from `GetLastError` should be retained immediately at
  the failing call boundary. Human-readable `FormatMessageW` output is useful
  for presentation but must not drive state transitions or retry decisions.
- Reopen must create a new logical generation. Any result produced by the old
  generation is settled for accounting but cannot mutate the replacement
  session, UI pending state, or protocol result.
- Automatic replay is not supplied by Win32 and is unsafe as a default after
  reconnect; accepted work from the old generation should terminate explicitly.

## Applicability Boundaries

- `microsoft/Windows-classic-samples` contains examples, not the normative
  Windows API contract. Final behavior must follow the existing project code and
  current Microsoft API semantics, especially for `CreateFileW`, DCB,
  COMMTIMEOUTS, `ReadFile`, `WriteFile`, `CancelIoEx`, `ClearCommError`,
  `PurgeComm`, and `CloseHandle`.
- The returned examples emphasize overlapped file/network I/O. Phase 2 currently
  preserves the project's synchronous serial backend and a future overlapped-I/O
  seam; sample IOCP or `OVERLAPPED` designs must not be copied into this phase
  merely because they appear in the repository.
- `CancelSynchronousIo` is thread-targeted and does not justify claims about
  `CancelIoEx`. The exact cancellation guarantee depends on how the COM handle
  was opened and how the operation was issued.
- COMMTIMEOUTS values have serial-specific and sometimes non-intuitive read
  semantics. A wait timeout, a zero-byte read, and a failed read must remain
  distinct until verified against the existing backend and Microsoft API
  documentation.
- `FormatMessageW` text can vary by system language and must remain diagnostic
  presentation only. Stable category plus native error code is the control-flow
  evidence.
- The samples do not establish reliable unplug/replug behavior across USB-
  serial drivers. Phase tests must verify only observable project guarantees and
  avoid claiming that a particular driver cancels or drains synchronously.
- DTR/RTS behavior depends on the DCB flow-control mode and driver support.
  Existing hardware-flow-control rejection and UI rollback rules remain the
  product authority.

## Guidance by Phase Task

### Task 1 — Harden Win32 Session Owner

- Move the device handle, configuration, control-line state, worker/event
  handles, queue, completion storage, synchronization, and shutdown into the one
  move-disabled `Win32SerialSession` object.
- Preserve the existing open/configure order and failure cleanup rather than
  introducing an overlapped rewrite during the rename.
- Delete old concrete-owner names and prevent native handle exposure; a
  temporary facade may reference the same object but must never own another
  handle.
- Confidence: **high** for the ownership boundary; **medium** for serial setup
  details because DeepWiki returned little DCB/COMMTIMEOUTS evidence.

### Task 2 — Implement Generation and Settlement

- Publish a generation only after open and configuration fully succeed. Invalidate
  it when closing begins, and attach it to every accepted operation and terminal
  result.
- Treat close as a protocol: reject new work, request cancellation/wake the
  worker, settle pending and active work exactly once, join the worker, then
  release events and the COM handle.
- Record native codes immediately and map them to stable categories. Preserve
  partial byte counts for failure, timeout, cancellation, and disconnect
  outcomes.
- Do not infer that `CancelIoEx` has completed the operation; settlement remains
  mandatory. Verify exact synchronous-handle behavior against Microsoft API
  semantics before coding that call path.
- Confidence: **medium-high** for lifecycle/generation architecture; **medium-low**
  for the precise cancellation mechanics until task-level API research.

### Task 3 — Migrate Main-Window Lifecycle

- Route open, close, endpoint snapshots, DTR/RTS, and session state through the
  typed lifecycle capability. The main window retains retry and presentation
  policy but performs no native handle operation.
- Keep flow-control validation at the existing boundary. A line-control failure
  should return structured native evidence, while UI localization and checkbox
  rollback remain UI responsibilities.
- Reconnect reuses selected options but never old request IDs or accepted work.
- Confidence: **high** for the ownership split; **medium** for driver-specific
  DTR/RTS effects.

### Task 4 — Migrate Main-Window I/O

- Match terminal write results by `(generation, requestId)`, not request ID or
  text alone. Settle stale results internally but suppress all stale UI effects.
- Keep receive batches bounded and preserve distinctions among data, timeout,
  disconnect, cancellation, and native failure. Never equate zero bytes with a
  universal disconnect rule without verified COMMTIMEOUTS semantics.
- Queue snapshots must include active work, and cancellation must not release a
  request's buffers or accounting before its terminal result is collected.
- Confidence: **high** for result matching and buffer lifetime; **medium** for
  read-timeout classification pending task-level verification.

### Task 5 — Migrate RTU and Modbus Borrowing

- The RTU adapter borrows only typed byte operations. It must not own a handle,
  event, queue, reconnect policy, or concrete Win32 session.
- Capture the generation when a scan starts and validate it before exchanges and
  result publication. A close/reopen transition terminates the old scan rather
  than converting stale bytes into a valid replacement-session frame.
- Preserve current frame assembly and timing policy; this research does not
  justify new CRC, retry, stale-RX purge, or device-decoding behavior.
- Confidence: **high** for the capability/generation boundary; **low** for any
  unplanned protocol timing change, which remains explicitly out of scope.

### Task 6 — Remove Broad Transport Facade

- Remove the broad facade only after lifecycle, UI I/O, and RTU/Modbus callers
  compile against narrow capabilities. Do not restore compatibility through an
  alias, forwarding class, or queue inheritance.
- Keep all Win32 APIs private to the concrete session. Typed status, category,
  native code, byte count, deadline, generation, and request ID replace
  localized error-text decisions.
- Structural searches and a clean full build are required because this step is
  principally an ownership and dependency-boundary proof.
- Confidence: **high**; the facade removal is an internal architecture decision
  and does not rely on missing serial-specific sample behavior.

## Phase-Level Confidence

Overall confidence: **medium-high** for the planned architecture and ownership
rules, **medium** for synchronous serial I/O and deadline behavior, and
**medium-low** for exact cancellation/unplug semantics until task-level research
verifies the relevant Microsoft API contracts. DeepWiki completed successfully,
so no WebSearch fallback was used.
