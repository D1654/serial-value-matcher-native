# Domain Research — Production Serial Transport Layer (C++20/Win32)
Generated: 2026-07-11T04:12:00+08:00

## Domain Overview

A Windows serial port is an exclusive byte-stream handle whose electrical
configuration (DCB, baud rate, parity, stop bits, flow control, DTR/RTS) is
different from protocol framing. `ReadFile` and `WriteFile` operate on bytes;
Modbus RTU, line protocols, and application commands must own framing, CRC,
transaction boundaries, and response validation above the byte stream. The UI
must not wait on device I/O. A production adapter therefore needs an explicit
owner for the handle, bounded waits, deterministic close behavior, and a result
model that preserves partial progress and native error information.

The current project is correctly serial-only and Qt-free: `SerialTransport`
is the production-facing contract, `Win32SerialPort` owns the Win32 handle and
write worker, and `SerialRtuTransport` maps byte operations to RTU exchanges.
The v2 opportunity is not adding TCP or another backend. It is tightening the
contract boundaries and lifecycle so reads, synchronous writes, queued writes,
control-line changes, cancellation, and reconnect cannot race each other or
hide a failure behind shared text state.

## Key Concepts & Terminology

- **Byte stream**: transport returns arbitrary chunks; it does not guarantee a
  protocol frame or message boundary.
- **Framer/codec**: protocol-owned state machine that accumulates chunks,
  determines expected length, validates CRC/checksum, and handles exceptions.
- **Session generation**: monotonically increasing ID assigned at each open;
  completions from an older handle must not be applied to a newer session.
- **Single owner**: one thread or executor serializes handle access and close;
  borrowed clients never own or close the native handle.
- **Overlapped I/O**: Win32 asynchronous operation using `OVERLAPPED`, an
  event/completion mechanism, and explicit completion collection.
- **Cancellation point**: a documented point where cancellation is observed;
  requesting cancellation is not the same as an operation having terminated.
- **Backpressure**: admission policy for bounded pending requests, preferably
  bounded by both request count and payload bytes.
- **In-flight request**: an accepted operation currently being written; it is
  distinct from queued pending requests and may have different cancellation
  semantics.
- **Typed error**: stable category (timeout, cancelled, closed, disconnected,
  framing/parity/overrun, queue-full, native failure) plus native code and
  partial byte count, rather than message-string classification.
- **Half-duplex transaction**: request and response share one serial channel;
  stale RX bytes and concurrent writers must be controlled at the transaction
  boundary.

## Common Architecture Patterns

### 1. Single I/O owner (recommended for this desktop tool)

All handle operations are serialized by one transport executor/thread. Callers
submit open, close, read, write, control-line, and cancellation commands and
receive completion records. The owner can use synchronous I/O with strict
timeouts first, or overlapped I/O with `CancelIoEx` as a later implementation.

**Pros:** one place owns HANDLE lifetime; close and cancellation ordering is
deterministic; no read/write/close lock matrix; UI and protocol workers cannot
accidentally access a closed handle. **Cons:** requires command/completion
plumbing and an explicit shutdown protocol.

### 2. Split capability contracts behind a facade

Keep a small lifecycle/session contract, a byte-stream read/write contract, a
queued-write contract, and an optional control-line capability. `SerialTransport`
can remain a convenience facade, but protocol code should depend only on the
smallest capability it needs.

**Pros:** fakes are simpler, protocol adapters cannot mutate DTR/RTS or queue
policy accidentally, and future serial implementations can omit unsupported
capabilities. **Cons:** more types and an explicit composition/lifetime rule.

### 3. Protocol-owned framing adapter

The transport exposes bytes and readiness/completion only. A protocol adapter
owns frame assembly, expected-length calculation, CRC, exception frames,
inter-frame timing, stale-buffer policy, and transaction timeout. RTU should not
depend on a transport-specific error string or assume every successful chunk is
a complete frame.

**Pros:** reusable transport, deterministic protocol tests, clear layering.
**Cons:** protocol adapters need an injectable clock and cancellation source.

### 4. Bounded queue with explicit admission and completion

Assign request IDs at admission, enforce both maximum outstanding requests and
maximum queued bytes, and expose accepted/rejected/completed events. Define
whether capacity includes the in-flight item. Cancellation of pending requests
is immediate; cancellation of an in-flight request is asynchronous and must
produce exactly one terminal completion.

**Pros:** predictable memory use and UI behavior under burst sends. **Cons:**
requires metrics and a policy for queue-full feedback.

### 5. Synchronous worker versus overlapped I/O

A dedicated worker around synchronous `ReadFile`/`WriteFile` is acceptable when
timeouts are finite, close waits for the worker, and cancellation latency is
bounded by those timeouts. Overlapped I/O is preferable when cancellation and
reconnect must interrupt long operations: issue the operation, wait for its
completion event or cancellation event, call `CancelIoEx` when requested, and
still collect the final completion before closing the handle.

**Trade-off:** overlapped I/O gives stronger interruption semantics but adds
operation-context lifetime and completion-race complexity. Do not mix unowned
direct handle calls with either model.

### 6. Structured operation results and observability

Return a value containing operation, endpoint, session generation, status,
typed error, native error code, bytes transferred, and timestamps. Keep a
human-readable localized message at the UI boundary. Record queue depth,
rejections, cancellation latency, timeouts, reconnects, and device errors; do
not log full payloads by default.

## Typical Challenges & Pitfalls

1. **Critical — close races with I/O.** `handle_`, `options_`, and
   `lastErrorText_` are not all protected by the write lock. A read, synchronous
   write, DTR/RTS call, or `close()` can overlap another operation. Use one I/O
   owner or a lifecycle gate that prevents new operations and waits for every
   active operation before handle destruction.
2. **High — cancellation only covers pending writes.** The current queue can
   cancel pending entries, while an in-flight synchronous `WriteFile` continues
   until its write timeout. Define this limitation, or move to overlapped I/O
   plus `CancelIoEx`; after requesting cancellation, always wait for terminal
   completion before reuse/close.
3. **High — per-request timeout is not authoritative.** A queued request stores
   `timeoutMs`, but the Win32 worker currently calls `writeBytesInternal`, which
   uses the port-wide `options_.writeTimeoutMs`. Either apply the request
   deadline in the owner or remove the misleading field.
4. **High — ambiguous queue capacity.** The admission check treats an
   in-progress item specially and only bounds request count. Specify whether
   capacity includes in-flight work and add a byte budget to prevent one large
   payload from exhausting memory.
5. **High — shared last-error text.** `lastErrorText()` is mutable global state;
   a later operation can overwrite the error being displayed for an earlier
   completion. Return immutable structured errors with each result.
6. **Medium — string-based error classification.** Checking for Chinese
   “超时” or Win32 code text is fragile and localization-dependent. Preserve a
   typed timeout/native-error category and map it to text only at the UI edge.
7. **Medium — polling has weak cancellation semantics.** `waitForReadyRead`
   polls `ClearCommError` and sleeps; cancellation is only checked between
   waits. An injectable wait/cancel primitive should bound latency and avoid
   spin when a fake or driver returns immediately with no bytes.
8. **Medium — stale receive bytes.** RTU transactions need an explicit policy
   for bytes already buffered before a request, and for bytes left after a
   timeout. Otherwise a late response can be attributed to the next request.
9. **Medium — transport decides too much framing.** `SerialRtuTransport`
   infers normal response length from FC03/FC04 requests. Move frame parsing,
   CRC, exception handling, and inter-frame timing into a protocol framer; the
   transport should only move bytes.
10. **Medium — reopen completion leakage.** Reopening after disconnect must
    cancel/complete old queued and in-flight requests and invalidate old
    callbacks. Session generations prevent stale events reaching a new handle.
11. **Medium — fake-only confidence.** Contract tests need deterministic
    concurrency, queue saturation, close/cancel races, short writes, read
    errors, unplug/replug, and malformed/late RTU frames in addition to normal
    chunking tests.
12. **Release — evidence drift.** Native unit tests, PTY loopback, real-device
    tests, package audit, and documentation must exercise the same transport
    contract and report stable status keys.

## Interview Must-Cover Topics

1. What is the required maximum cancellation-to-terminal-completion latency?
2. Is the transport allowed one I/O owner thread, and must all reads/writes be
   serialized or can independent read/write operations overlap?
3. Does queue capacity mean requests, bytes, or both, and what should the UI do
   on `RejectedFull`?
4. Are per-request write deadlines required, and how should a timeout affect
   the handle and subsequent queued requests?
5. Which operations are cancellable (pending, in-flight write, read wait,
   protocol transaction), and what terminal result is guaranteed exactly once?
6. What is the stale-RX policy after timeout, cancellation, disconnect, and
   reopen (purge, drain, or hand bytes to the next protocol transaction)?
7. Which protocol responsibilities belong above transport: framing, CRC,
   exception responses, inter-frame delay, retries, and transaction locking?
8. What error categories and native diagnostics must be persisted in logs and
   shown to users, and which payload data is sensitive?
9. What reconnect/session-generation guarantees are required for workers and
   UI callbacks after a device is unplugged or renamed?
10. Which evidence gates are mandatory for release: fake contract tests,
    MinGW/Wine CTest, PTY matrix, hardware loopback, stress duration, package
    audit, and no-Qt dependency scan?

## Sources

WebSearch queries attempted (the web tool returned a decode/stream error in this
environment; canonical primary URLs are listed for consolidation verification):

- `Windows serial communications overlapped I/O CancelIoEx Microsoft Learn`
- `Win32 serial SetCommMask WaitCommEvent ClearCommError Microsoft Learn`
- `Win32 ReadFile WriteFile COMMTIMEOUTS serial handle Microsoft Learn`
- `C++ serial port asynchronous cancellation bounded queue production design`

Primary references:

- https://learn.microsoft.com/en-us/windows/win32/serial/serial-communications
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile
- https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-commtimeouts
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-clearcommerror
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-waitcommevent
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setcommstate
- https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-dcb
- https://www.boost.org/doc/libs/1_85_0/doc/html/boost_asio/reference/serial_port.html
- https://www.boost.org/doc/libs/1_85_0/doc/html/boost_asio/reference/basic_serial_port/cancel.html

Existing repository research cache consulted:

- `.workflow/deepwiki-cache/microsoft_Windows-classic-samples__Core_Win32_serial_communication_patterns_and_.md`
- `.workflow/deepwiki-cache/microsoft_Windows-classic-samples__For_Win32_file_or_serial_handles__what_are_th.md`

## Summary

The project already has the right high-level direction: a Qt-free serial
contract, one Win32 production adapter, and a Modbus adapter. The next
architecture step should make ownership and completion semantics explicit, not
add another transport. Prefer a single I/O owner that serializes HANDLE access
and close, then expose smaller lifecycle, byte-stream, read, queued-write, and
control-line capabilities behind a facade. Keep framing, CRC, stale-RX policy,
retries, and transaction timeouts in protocol adapters. Define queue capacity in
both requests and bytes; give every accepted request a unique session-aware
terminal result. Replace shared localized error text and text matching with
typed errors carrying native codes and partial byte counts. If cancellation
latency must be below the current synchronous write timeout, use overlapped I/O
and `CancelIoEx`, while still collecting the final completion before closing a
handle. Verify the design with deterministic fake transports, concurrent
close/cancel/reopen tests, queue saturation, malformed and late RTU frames,
PTY loopback, real hardware unplug/replug, MinGW/Wine CTest, package audit, and
stable release evidence. These decisions preserve the serial-only scope while
making a future backend possible without forcing TCP or UDP into this product.
