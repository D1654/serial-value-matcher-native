# BS-4: Runtime and Design Strategy

Completed: 2026-07-12T08:15:00+08:00

## Research Findings

- Windows serial cancellation and timeout behavior depends on the operation
  thread, handle lifetime, and driver behavior; the design must make those
  boundaries explicit.
- The existing test strategy already provides the right deterministic base:
  fake contracts, PTY loopback, and MinGW/Wine gates.
- WebSearch returned a decode error in this environment; official-source
  fallback and prior DeepWiki results were used.

## Multi-Perspective Evaluation

- User/Product: every send should move from accepted to one clear final result,
  so the UI can explain what happened.
- Developer: a small session state machine is safer than scattered booleans
  such as `isOpen` plus shared error text.
- Architect: generation belongs to the session identity, not to UI callbacks.
- Security: do not log raw payload by default; retain operation and native
  error evidence only.
- Ops/SRE: test unplug, close, cancel, timeout, and reopen as sequences, not
  just isolated happy paths.
- Maintainer: no automatic retry or replay keeps behavior predictable and code
  small.

## Self-Interrogation

Initial recommendation: use a small session state machine with one owner,
explicit operation deadlines, exactly-once terminal results, generation checks,
and a queue that counts both requests and bytes.

Challenge 1: A large manual payload may exceed the byte budget and surprise a
user. Response: reject it immediately with a clear typed result; do not silently
split or discard it. The UI can later offer an explicit file/chunk workflow.

Challenge 2: One owner may serialize read and write work more than necessary.
Response: correctness comes first for the serial diagnostic base; the byte
session can later evolve internally without changing its result contracts.

Challenge 3: A driver may not settle a synchronous call within one second.
Response: use the explicit deadline/cancellation path, test the target, record
the native failure, and retain an isolated overlapped-I/O evolution point rather
than hiding an unbounded wait.

## Decision

Implement a simple state machine: closed, opening, open, closing, and faulted.
Every accepted write/read operation carries request ID, generation, deadline,
and a typed terminal result. Reconnect invalidates the old generation before a
new one can publish results. Backpressure and cancellation are deterministic;
retry/replay remains the caller's responsibility.

Confidence: High for the state and result rules; medium for driver-specific
sub-second cancellation until hardware smoke tests are available.
