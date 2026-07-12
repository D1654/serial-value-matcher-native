# Draft Cache: Serial Transport Layer v2

Draft phase started: 2026-07-12T07:16:14+08:00

The draft is being prepared from the confirmed requirements. It will remain a
plain-language architecture proposal; no source code changes occur in this
phase.

## Section 1: Project Overview

This is a local serial diagnostic tool for PLCs and other serial devices. The
first goal is a dependable serial foundation: one place owns the connection,
operations finish clearly, reconnects cannot mix old and new data, and queues
cannot grow without limit. More scanning and decoding features come later.

## Section 2: Architecture Design

Use one small `SerialSession` as the only owner of the native serial handle.
The UI, Modbus code, and command sequence submit requests and receive results;
they never touch the handle directly. `SerialWriteQueue` stays a focused
bounded queue inside the session. `SerialRtuTransport` and future device
decoders remain above the byte session and do not enter the transport code.

```text
UI / command sequence / Modbus
            |
      byte requests + results
            v
      SerialSession
  handle + lifecycle + generation
      bounded write queue
            |
      Win32 serial API
```

The old broad `SerialTransport` facade is removed, not retained as a second
API. The design uses only boundaries that prevent real races; it does not add
a generic framework.

## Section 3: Technology Selection

Keep C++20, direct Win32 serial APIs, the standard library, CMake/CTest, and
the existing MinGW/Wine checks. Use a small move-only RAII handle owner inside
`SerialSession`; do not add Boost.Asio, WIL, or another runtime dependency.
This keeps the package small and preserves the current build and release gates.

## Section 4: Runtime and Design Strategy

Use a small session state machine: `closed`, `opening`, `open`, `closing`, and
`faulted`. Every accepted operation gets a request ID, session generation,
deadline, and exactly one final result. Reconnect invalidates the old generation
before the new session publishes anything. The queue rejects over-limit work
immediately; it never silently retries or drops requests. Raw payload is not
logged by default.

## Section 5: Production and Verification Strategy

Keep the current local release model. Run fake contract tests and PTY tests,
then MinGW/Wine, UI/self-test, package, size, hash, and documentation checks.
Add focused cases for close/cancel/reopen races, deadlines, byte backpressure,
stale generations, short writes, and typed errors. Real PLC hardware remains an
optional local smoke test; CI and release do not depend on it. No network,
telemetry, database, container, or deployment service is added.

## Section 6: Project Structure

Keep the existing `src/transport`, `src/win32`, and `tests` layout. The
transport area contains the session contracts, Win32 session implementation,
write queue, and neutral result types. RTU adapters remain separate. Tests are
split into pure contract tests and PTY/native integration tests.

## Section 7: Implementation Phases

1. **Session contract and ownership** — define state, generation, typed results,
   deadlines, and the single owner; about 5 tasks.
2. **Caller migration and facade removal** — move UI, Modbus, and command
   sequence callers; remove the broad facade; 6 detailed tasks after BS-6.
3. **Production hardening** — add queue byte limits, cancellation/close rules,
   reconnect tests, PTY cases, and release gates; about 6 tasks.
4. **Future codec preparation** — document the upper-layer bit-layout/codec
   boundary without implementing Gray code in transport; about 2 tasks.

## Section 8: Risk Assessment

- A driver may exceed the one-second cancellation target; mitigate with native
  evidence, hardware smoke tests, and a future overlapped-I/O seam.
- A close/reconnect race could duplicate or lose a result; mitigate with one
  owner, generation checks, and exactly-once tests.
- The byte budget may reject unusually large manual sends; report it clearly and
  provide explicit chunk/file workflows later.
- Stale RTU data could be mistaken for a new response; keep stale-frame policy
  in the RTU layer and test it separately.
- CI has no PLC hardware; use fake/PTY tests and label hardware coverage as
  optional rather than weakening the release gate.

## Section 9: Complexity Estimate

Overall complexity: **Medium**. Four implementation phases and 18 focused
tasks after decomposition review. The hard part is lifecycle correctness, not
adding features.
