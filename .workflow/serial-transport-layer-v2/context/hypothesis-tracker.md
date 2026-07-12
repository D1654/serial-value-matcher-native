# Hypothesis Tracker: Serial Transport Layer v2

Updated: 2026-07-12T08:30:00+08:00

| ID | Hypothesis | Status | Evidence | Decision impact |
| --- | --- | --- | --- | --- |
| H1 | One session owner should serialize the native handle lifecycle and I/O. | CONFIRMED | User made a single owner and serialized HANDLE access hard constraints. | The v2 design must have one owner for lifecycle, reads, writes, and control lines. |
| H2 | Narrow capabilities should replace the broad `SerialTransport` facade; no compatibility wrapper is needed. | CONFIRMED | User explicitly rejects redundant compatibility code and accepts direct migration of project callers. | The plan may remove the old facade and update all in-repo callers/tests in one controlled change. |
| H3 | Structured operation results should replace shared localized error text for transport decisions. | CONFIRMED | User confirmed structured results, native error evidence, UI-only localization, and no downstream text parsing. | Transport and workers must branch on typed status/category, never localized messages. |
| H4 | Bounded cancellation latency requires a direct Win32 event/overlapped-I/O evolution. | REJECTED | User accepted a `<= 1 second` terminal-result target without making full overlapped I/O part of this delivery. | The v2 contract must leave an evolution seam, but the first implementation prioritizes explicit synchronous timeout/cancel behavior. |
| H5 | Queue admission must use both request and byte budgets, with explicit in-flight accounting. | CONFIRMED | User confirmed 64-request and 256 KiB defaults, immediate rejection, and no implicit dropping. | The queue contract must expose both budgets and account for the active request. |
| H6 | Session generations are needed to suppress stale completion after reconnect. | CONFIRMED | User confirmed generation increments, termination of old requests, stale-result suppression, and no automatic replay. | Every operation/result must carry session identity; reconnect is a new logical session. |
| H7 | TCP/UDP, Qt, Boost.Asio, WIL, and new runtime dependencies remain excluded. | CONFIRMED | User request and Phase 0 scope. | Keeps plan serial-only and package-compatible. |
| H8 | Modbus framing/CRC/stale-RX policy should stay above the byte transport. | CONFIRMED | User confirmed a stable serial base first and protocol/encoding capabilities above it. | v2 must not add protocol parsing or device-specific framing to the byte layer. |
| H9 | Scanner/matcher needs an upper-layer codec and bit-layout model for variable encodings such as Gray code. | OPEN | User identified non-fixed data bits and Gray-code devices as a concrete future requirement. | Future feature planning must extend decoding rules without changing transport ownership or byte contracts. |
