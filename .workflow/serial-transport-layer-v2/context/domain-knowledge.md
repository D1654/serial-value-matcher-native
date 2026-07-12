# Domain Knowledge: Serial Transport Layer v2

Updated: 2026-07-12T08:30:00+08:00

## Consolidated Domain Summary

The application is a local Win32 serial diagnostic tool. A serial port is an
exclusive byte stream: open/close, DCB configuration, DTR/RTS, byte I/O,
timeouts, cancellation, write scheduling, protocol framing, UI rendering, and
evidence persistence are separate responsibilities. The next iteration is not
another transport implementation. It is a hardening pass over the existing
serial-only stack so that no UI, protocol worker, reconnect path, or queued
write can race the native handle lifecycle.

The current baseline has the right initial elements: neutral serial types,
`SerialWritePort`, `SerialTransport`, `Win32SerialPort`, `SerialRtuTransport`,
fake contract tests, PTY loopback, and package gates. The central design
question is how far to evolve the current broad facade into narrow contracts
and a single session owner while preserving all current user-visible behavior.

The product direction is a serial diagnostic base for PLCs and general serial
devices. The transport foundation must be stabilized before adding richer
matching features. Scanner/matcher evolution should eventually support variable
bit layouts and multiple value encodings, such as Gray code, through an upper
layer codec/bit-layout model rather than transport-specific branches.

## Confirmed Constraints

- Keep C++20, Win32 native, CMake, CTest, MinGW/Wine, GitHub Actions, and the
  current package size/security gates.
- Keep a serial-only implementation. TCP, UDP, network services, Qt, SQLite,
  plugins, and new default runtime dependencies are out of scope.
- Preserve current manual/timed/file sending, receive polling, Modbus RTU
  scans, command-sequence foundation, reconnect behavior, raw evidence, PTY
  scenarios, and native release evidence.
- Do not introduce Boost.Asio or WIL in this workflow without a future,
  explicit dependency decision gate.

## Research Findings

### Ownership and lifecycle

Production serial implementations converge on one owner for the device handle.
That owner serializes open, close, read, write, control-line changes, and
cancellation. Borrowed clients receive immutable results/snapshots rather than
directly coordinating HANDLE lifetime. Close/reopen must settle or invalidate
queued and in-flight work before a new session can use the port.

Useful terms:

- **Session generation:** monotonic ID per successful open; old completions
  cannot mutate a later reconnect.
- **Terminal result:** every accepted operation completes exactly once as sent,
  failed, timed out, cancelled, or closed, retaining byte count and native
  diagnostic data.
- **In-flight request:** distinct from pending queue entries and potentially
  subject to different cancellation guarantees.

### Contract boundaries

Recommended internal responsibilities are:

1. A serial session/lifecycle capability: configuration, state, endpoint,
   generation, and close-drain behavior.
2. A byte-stream capability: structured read/write results and deadlines.
3. A write-scheduler capability: bounded admission, cancellation, completion,
   queue count/byte watermarks, and active request identity.
4. A UI-neutral observer/evidence capability: state and operation events with
   endpoint, generation, request ID, time, byte count, status, and native code.

The old broad `SerialTransport` facade is removed; in-repo consumers migrate
directly to the small session and byte-operation contracts. Protocol adapters
depend only on byte operations; they do not own a port, UI reconnect policy, or
a write queue.

### Confirmed v2 runtime rules

- The session state is explicit: closed, opening, open, closing, or faulted.
- Every accepted operation completes exactly once with request ID, generation,
  deadline, status, and native error evidence where applicable.
- Reconnect invalidates the old generation; no automatic replay occurs.
- Queue limits are 64 requests and 256 KiB of counted work, including the active
  request; over-limit admission is rejected immediately.
- The current delivery keeps synchronous Win32 I/O with a future overlapped-I/O
  seam; it does not claim unverified driver behavior.

### Current risks to resolve or explicitly defer

- Handle, options, and shared `lastErrorText` do not currently share a single
  lifetime/concurrency boundary.
- Pending-write cancellation is defined, but in-flight cancellation and close
  ordering need an explicit guarantee.
- Per-request write timeout is stored but the worker currently relies on the
  port-wide timeout.
- Queue capacity is count-based and needs an explicit in-flight rule and, if
  required, a pending-byte budget.
- Error classification relies in part on localized message text; stable error
  categories and native error codes are preferable.
- Modbus RTU currently owns only basic expected-length accumulation. Frame
  validation, stale RX policy, inter-byte timing, and retry ownership need
  explicit architectural decisions before changing behavior.
- Reconnect needs a generation-aware rule so old callbacks/completions cannot
  affect the replacement session.
- Data scanning/matching is a later feature track: encoding and bit-layout
  variation must remain above transport and must not expand the v2 byte-layer
  contract.

### Backends and dependencies

The recommended v2 implementation stays on direct Win32 communications APIs.
The C++ standard library is appropriate for contracts, threads, clocks, and
containers but cannot replace the Win32 serial backend. Event-based overlapped
I/O plus `CancelIoEx` is a possible later backend evolution when bounded
in-flight cancellation requires it. It must collect final completion before
reusing operation buffers or closing the handle.

Boost.Asio and WIL were considered but are not default choices. Boost.Asio adds
executor/completion-token migration and supply-chain/build gates; WIL reduces
Win32 RAII boilerplate but does not solve serial semantics and needs separate
MinGW/package validation. DeepWiki checks for these candidates and the Windows
classic samples completed during the workflow; their findings and limitations
are recorded in the brainstorm artifacts. Official documentation remains the
fallback for API semantics.

## Competitive Expectations

Users of terminal and Modbus diagnostic tools expect visible connection state,
serial configuration, request/response timing, clear timeout/exception text,
repeatable sends, persistent evidence, and diagnosable unplug/reconnect
failures. Mature libraries distinguish enqueue acceptance from physical write
completion, expose partial bytes and deadlines, and avoid letting UI code race
the device object.

## Initial Test Strategy

The plan should preserve the existing fake, MinGW/Wine, PTY, self-test, UI
performance, package, and documentation gates, then add focused tests for:

- close/cancel/reopen races and exactly-once terminal completions;
- count/byte backpressure and per-request deadlines;
- stale-generation completion rejection;
- short writes, read errors, unplug/replug, and driver timeout categories;
- RTU malformed/late/stale-frame behavior only after its policy is confirmed.

## Sources and Research Status

Agent reports:

- `agent-outputs/agent-a-domain.md`
- `agent-outputs/agent-b-competitive.md`
- `agent-outputs/agent-c-tech.md`

Primary sources used or recorded for fallback:

- Microsoft Learn: serial communications, `ReadFile`, `WriteFile`,
  `CancelIoEx`, `COMMTIMEOUTS`, `ClearCommError`, and communications resources.
- Boost.Asio serial port and cancellation references.
- Microsoft WIL README and RAII/error-handling documentation.
- pySerial, jSerialComm, and libmodbus documentation for behavioral comparison.

WebSearch returned a decode error during this workflow. DeepWiki checks for the
Windows classic samples, Boost.Asio, and WIL completed; their limitations are
recorded in the brainstorm artifacts. Official Microsoft source references
remain the fallback for Windows API semantics.
