# BS-2: Serial Foundation Architecture

Completed: 2026-07-12T07:16:14+08:00

## Research Findings

- Microsoft Windows samples show that synchronous I/O can be cancelled through
  a dedicated cancellation owner (`CancelSynchronousIo`) when the operation
  thread and handle lifetime are controlled carefully.
- Boost.Asio provides asynchronous serial I/O and cancellation, but it brings
  an executor/IOCP model and a new dependency. That is not a good fit for the
  current small native package and is excluded.
- WebSearch returned a decode error in this environment. The earlier official
  source fallback and the successful DeepWiki checks were used instead.

## Multi-Perspective Evaluation

- User/Product: a single owner directly reduces the most visible failures:
  disconnect races, mixed reconnect data, and sends that appear accepted but
  never finish.
- Developer: one session worker is easier to test than several locks around a
  broad facade; the queue can remain a small independent value type.
- Architect: keep transport byte-only and place RTU framing and future codecs
  above it, so Gray code does not expand the transport contract.
- Security: staying on direct Win32 and the standard library avoids extra
  runtime supply-chain and network surface.
- Ops/SRE: fake and PTY tests can deterministically exercise close, cancel,
  timeout, and reconnect without requiring a PLC in CI.
- Maintainer: removing the old compatibility facade prevents two competing
  APIs and keeps the ownership rule visible in the type structure.

## Self-Interrogation

Initial recommendation: replace the broad facade with one `SerialSession`
owner, keep a bounded `SerialWriteQueue` inside it, and let protocol adapters
consume byte operations only.

Challenge 1: If a device driver ignores a synchronous timeout, a strict
one-second physical completion guarantee may not be possible without overlapped
I/O. Response: v2 must use explicit deadlines plus `CancelSynchronousIo` where
available, test the one-second target, and keep an isolated future seam for
overlapped I/O. It must not claim stronger hardware behavior than verified.

Challenge 2: Splitting every responsibility into many interfaces could recreate
the code bloat the user rejects. Response: use only the session, queue, and
protocol adapter boundaries that remove real races; do not add a generic
transport framework or compatibility wrapper.

Challenge 3: Future Gray-code and variable-bit matching could leak into the
transport. Response: the session returns bytes and typed transport results;
future scanner codecs receive decoded fields above that boundary.

## Decision

Use a serial-only `SerialSession` with one owner for the native handle and all
handle operations. Keep the queue bounded by count and bytes, attach generation
to every operation, and return structured results. Remove the old broad
`SerialTransport` facade rather than preserving it for compatibility. Keep
RTU/protocol parsing and future value codecs above the byte session.

Confidence: High for ownership and layering; medium for the exact cancellation
behavior on every third-party driver until hardware smoke tests exist.

Unverified assumption: the current supported Windows driver set honors the
chosen synchronous cancellation/deadline path within the target.
