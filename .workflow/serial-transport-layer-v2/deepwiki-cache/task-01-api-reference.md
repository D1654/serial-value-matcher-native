# API Reference - Task 01: Document Codec Boundary

Generated: 2026-07-20

## Dependency Summary

Task 01 declares no external library or framework dependency. No library API is
called or configured. The research therefore reconciles documentation with the
checked-in source tree instead of inventing an API reference.

| Area | Source | Confidence |
|---|---|---|
| Session ownership | Local source inspection | High |
| Queue and typed results | Local source inspection | High |
| RTU/protocol boundary | Local source inspection | High |
| Gray/codec boundary | Local source inspection | High |

## Implemented Source Contracts

### Narrow Session Views

**Source:** `src/transport/serial_session.h`

- `SerialSession` owns lifecycle and exposes `open`, `close`, snapshots, DTR,
  RTS, and non-owning `SerialByteStream`/`SerialWriteScheduler` references.
- `SerialByteStream` exposes only byte reads and writes with typed deadlines.
- `SerialWriteScheduler` exposes admission, pending cancellation, terminal
  collection, and queue snapshots.

These neutral contracts contain no HWND, HANDLE, Win32 implementation, UI,
matching, codec, or persistence dependency.

### Win32 Owner

**Source:** `src/win32/win32_serial_session.h` and `.cpp`

- `Win32SerialSession` is the production `SerialSession`, `SerialByteStream`,
  and `SerialWriteScheduler` implementation.
- It is the sole Windows HANDLE owner and owns generation changes, typed native
  results, the bounded write queue, worker settlement, and control-line calls.
- Callers borrow the narrow references; they do not own or close the native
  handle directly.

### Queue And Result Evidence

**Source:** `src/transport/serial_types.h` and
`src/transport/serial_write_queue.h`

- Results carry operation identity, session generation, status, deadline,
  native error, communication-mask, and driver-queue evidence.
- Queue accounting includes pending and active request counts and bytes, with
  fixed count/byte limits and exactly-once terminal settlement.

### RTU And Protocol Boundary

**Source:** `src/transport/serial_rtu_transport.h` and `.cpp`, plus
`src/core/modbus_core.*`

- `SerialRtuTransport` borrows `SerialByteStream` and maps byte operations into
  the core Modbus RTU exchange contract.
- RTU framing, CRC, stale receive handling, retries, and protocol error
  classification stay above the neutral byte/session contracts.

## Codec And Gray-Code Facts

- The project already has a fixed `Gray16` numeric candidate in
  `src/core/analysis_core.*`. That is upper-layer analysis behavior and is not
  transport-v2 decoding.
- A general device codec for variable bit locations, binary/Gray/other
  encodings, signedness, scaling, and device-specific interpretation is not
  implemented by transport v2.
- Task 01 must not claim that all Gray-code support is absent from the project.
  It must state precisely that transport v2 does not decode Gray code and that
  this workflow adds no new codec UI control or persistence format.
- `NativeSendCodec` is the existing send-text/byte conversion module; it is not
  the future scanner/matcher field codec.

## Documentation Guidance

1. Describe `Win32SerialSession` as the sole native owner and the three neutral
   interfaces as narrow borrowed views.
2. Keep protocol and codec dependency arrows pointing toward byte results;
   transport must never depend on protocol, matcher, codec, UI, or storage.
3. Place future variable-layout codec work above RTU/transport, with its own
   deterministic tests.
4. Distinguish CTest and local Wine/PTTY evidence from optional physical-device
   smoke testing.
