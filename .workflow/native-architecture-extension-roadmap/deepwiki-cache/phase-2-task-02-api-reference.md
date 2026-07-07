# Phase 2 Task 02 API Reference

Generated at: 2026-07-07T18:52:35+08:00

## Scope

Task: Integrate Async Write Queue

Goal: Route manual send and file send writes through the bounded serial write queue so the UI thread no longer waits on serial write completion.

## DeepWiki References

### Win32 Serial Write Behavior

Reference target: `microsoft/Windows-classic-samples`

Relevant APIs:
- `WriteFile`
- `OVERLAPPED`
- `ERROR_IO_PENDING`
- `GetOverlappedResult`
- `CancelIo` / `CancelIoEx`
- serial `COMMTIMEOUTS`

Findings:
- A blocking `WriteFile` can stall the caller until the driver accepts or times out the write.
- An overlapped `WriteFile` may return `ERROR_IO_PENDING`; completion must be observed by an event, completion port, or `GetOverlappedResult`.
- Cancellation must be explicit and still requires observing the final completion/cancel state.
- Timeout and failure should be surfaced as separate terminal outcomes instead of being collapsed into a generic send failure.

Task decision:
- Do not convert the existing serial handle to full overlapped mode in this task. The current native port also owns synchronous read/poll behavior; changing handle mode would widen the task into receive-path and close/cancel semantics.
- Introduce a bounded write queue plus a serial write worker inside the native serial port. The UI thread only enqueues writes and drains completion results. This preserves the current receive path while eliminating UI-thread write waits.

### Qt SerialPort Error Semantics Reference

Reference target: `qt/qtserialport`

Relevant concepts:
- write acceptance vs. bytes-written completion
- timeout/error reporting
- cancel/close interaction

Findings:
- The application should distinguish "accepted for writing" from "physically written".
- Completion results need stable states for sent, timeout, failed, cancelled, invalid request, and backpressure/full queue.
- UI-level send logs and counters should update only after terminal sent completion, not when a request is merely accepted.

Task decision:
- Manual send returns immediate "queued/accepted" feedback and final TX log/counters after completion.
- File send keeps at most one chunk in flight; it advances progress only after the write completion is drained.
- Backpressure is reported synchronously when enqueue fails.
