# API Reference - Phase 2 Task 04: Unify Native Modbus Worker Adapter

Generated: 2026-07-08T15:18:00+08:00

## Scope

Task 04 has no external library dependency. The task uses existing internal Modbus protocol code and must preserve the Win32 native release boundary.

## Internal API Boundary

| Area | Current Source | Decision |
|------|----------------|----------|
| RTU request/response parsing | `src/core/modbus_core.*` | Use as the dependency-free protocol source of truth for native code. |
| Qt executor facade | `src/modbus/modbus_scan_executor.*` | Keep Qt-facing types stable, but delegate behavior to a pure core executor. |
| Native worker | `src/win32/native_modbus_scan_worker.*` | Keep as thread, Win32 serial transport, UI progress, and storage adapter only. |
| Native release dependency budget | `svm-native-win32` CMake target | Do not introduce Qt runtime, vcpkg, or new third-party runtime dependencies. |

## Task Decision

The existing `svm::modbus::ModbusScanExecutor` uses Qt containers and timestamps, so calling it directly from `native_modbus_scan_worker` would violate the project constraint that the GitHub Actions Win32 native package remains lightweight and free of Qt runtime dependencies.

Task 04 therefore extracts the reusable Modbus transaction loop into a new dependency-free core executor and keeps the Qt executor as an adapter. The native worker uses the same core executor through a Win32 serial transport adapter, while preserving existing progress messages, cancellation checks, raw IO traces, and storage records.

## Verification Focus

- Core executor owns retry, timeout, Modbus exception, parse error, and transport error behavior.
- Qt executor tests continue to pass through the adapter facade.
- Native scan request tests continue to prove the plan/result data boundary.
- The native Win32 target continues to link only slim core, Win32 serial, and native storage libraries.
