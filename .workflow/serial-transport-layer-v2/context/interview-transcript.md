# Requirements Interview: Serial Transport Layer v2

Started: 2026-07-11T06:48:18+08:00

## Known Baseline

- The scope remains serial-only. TCP, UDP, Qt, Boost, WIL, plugins, and new runtime dependencies are excluded.
- The current `SerialTransport` facade combines port lifecycle, synchronous reads, queued writes, cancellation, configuration, and error reporting.
- The desired optimization is an internal unified transport-layer architecture. Existing serial UI, Modbus, command sequence, reconnect, evidence, PTY, package, and CI behavior must remain available.
- Pre-research recommends a single session owner for the serial handle, narrow contracts beneath the compatibility facade, typed operation results, bounded queues by request count and bytes, and session generations to prevent stale callbacks.

## Question 1

Question: Cancellation or disconnection terminal results must return within what maximum time, and is full overlapped I/O required in this optimization?

Answer: The target is `<= 1 second`. Full overlapped I/O is not required in this optimization; the first implementation should establish explicit timeout and cancellation contracts while preserving a future evolution boundary.

## Question 2

Question: Must the existing `SerialTransport` class and main call patterns remain as a compatibility facade?

Answer: No. Do not retain redundant compatibility code. Migrate project callers directly, preserve required behavior and tests, and do not promise source compatibility for the old broad interface.

## Question 3

Question: Should every HANDLE operation (`open/close/read/write/DTR/RTS`) be owned by one session executor with serialized access?

Answer: Confirmed. A single owner and serialized handle operations are hard constraints for the unified transport layer.

## Question 4

Question: Should the send queue enforce both request-count and pending-byte limits, count the in-flight request, and reject over-limit requests immediately without blocking or dropping older requests?

Answer: Confirmed. Use both hard limits with immediate rejection. Defaults are 64 requests and 256 KiB total pending bytes.

## Question 5

Question: On reconnect, should the old session close, increment its generation, terminate old requests, reject stale completions, and avoid automatic replay?

Answer: Confirmed. Old requests terminate as `Cancelled` or `Disconnected`; stale results cannot update the new session; retries are explicit.

## Question 6

Question: Should transport return structured errors and native evidence while leaving localized text to the UI boundary?

Answer: Confirmed. Results carry structured status, operation, request ID, generation, and Win32 error evidence. UI owns localization; downstream code must not parse error text. Payload is not logged by default.

## Question 7

Question: Should transport remain a stable serial byte foundation while PLC/general-device protocols, bit layouts, and encodings stay in upper layers?

Answer: Confirmed. The product is a serial diagnostic tool for PLCs and other serial devices. Stabilize the serial base first, then expand features. Future scanning/matching must support variable bit layouts and pluggable encodings such as Gray code, but those features do not enter the transport implementation in this delivery.

## Requirements Interview Closure

The user found the earlier terminology too difficult. The interview is closed
with the confirmed plain-language direction above. Existing automated checks
(fake contracts, PTY, MinGW/Wine, package and documentation gates) remain the
default verification baseline; no hardware dependency is added to the plan.
