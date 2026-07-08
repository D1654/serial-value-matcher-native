# API Reference - Phase 2 Task 05: Normalize Modbus Result Semantics

Generated: 2026-07-08T16:54:00+08:00

## Scope

Task 05 has no external dependency. It normalizes internal Modbus result semantics using the repository's existing parser, scan plan builder, native UI state, and tests.

## Internal Decisions

| Area | Decision |
|------|----------|
| Response parsing | Add stable response result categories for success, CRC/frame errors, Modbus exception, identity mismatch, byte-count mismatch, register-count mismatch, and invalid expected request. |
| Scan planning | Add stable plan build categories for invalid slave/function/range/block/interval/retry, oversized plan, and request-build failure. |
| Native UI mapping | Add a small native attempt-result classifier that maps persisted attempt status plus exception/error context to success, timeout, retry exhausted, Modbus exception, frame/CRC error, data-format error, transport error, cancelled, or unknown. |

## Verification Focus

- `modbus_read_response_tests` asserts parser categories, not only text fragments.
- `modbus_scan_plan_tests` asserts plan validation categories, not only text fragments.
- `native_modbus_scan_ui_state_tests` asserts native UI result mapping for success, timeout, retry exhausted, Modbus exception, CRC/frame, data-format, transport, and cancellation.
