# Phase 3 Task 03 API Reference

## Research Record

Live DeepWiki `ask` calls completed successfully on 2026-07-17 for all required
repositories:

| Repository | Focus | Result |
|---|---|---|
| `microsoft/Windows-classic-samples` | `GetLastError`, `FormatMessageW`, `ClearCommError`, `COMSTAT`, `SetCommTimeouts` | Completed; the indexed samples covered immediate last-error capture and bounded message formatting, but did not contain direct `ClearCommError`/`SetCommTimeouts` examples. |
| `Kitware/CMake` | `add_test`, `ctest -R`, `--output-on-failure` | Completed. |
| `wine-mirror/wine` | `wine`, `WINEPREFIX`, headless UI execution | Completed; `xvfb-run` itself was outside the indexed snippets. |
| `actions/runner` | native exit codes, `GITHUB_STEP_SUMMARY`, evidence uploads | Completed. |

The missing Windows sample detail was supplemented from the Phase 3 cache, the
Task 02 API cache, the checked-in Win32 implementation, and the local MinGW
headers. Project tests and scripts remain authoritative where generic repository
guidance differs from this application.

## Exact Win32 Boundary

Relevant declarations:

```cpp
DWORD GetLastError();
DWORD FormatMessageW(
    DWORD flags,
    LPCVOID source,
    DWORD messageId,
    DWORD languageId,
    LPWSTR buffer,
    DWORD bufferChars,
    va_list* arguments);
BOOL ClearCommError(HANDLE file, LPDWORD errors, LPCOMSTAT status);
BOOL SetCommTimeouts(HANDLE file, LPCOMMTIMEOUTS timeouts);
```

`COMSTAT` exposes hold-state flags plus `cbInQue` and `cbOutQue`. `COMMTIMEOUTS`
contains read interval/total fields and write multiplier/constant fields.

### Failure capture

- Inspect `GetLastError()` only when the immediately preceding API contract says
  its return value is failure. Copy it before formatting, logging, locking, or
  calling another Win32 API.
- Preserve the copied numeric code in typed evidence. Formatting failure must
  never replace the original code or change the operation classification.
- `FormatMessageW` belongs at the Win32 UI boundary, not in transport code or the
  serial worker. Use `FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS`
  with a fixed-size buffer, then trim, sanitize, and clip the returned text.
- Control flow must use status/category/code. It must never inspect the localized
  message returned by `FormatMessageW` or any Chinese UI string.

### `ClearCommError` and `COMSTAT`

- On `FALSE`, capture `GetLastError()` immediately as `nativeCode`.
- On `TRUE`, `errors` is a `CE_*` communications-error bit mask, not a Win32
  last-error code. Do not store that mask in `nativeCode` and do not pass it to
  `FormatMessageW`.
- If the mask must be retained, use a distinct neutral integer field such as
  `commErrorMask`. Preserve `cbInQue` and `cbOutQue` in separate bounded numeric
  diagnostic fields. Do not expose `COMSTAT`, `DWORD`, or `HANDLE` above Win32.
- The current `readAvailable` path assigns `errors` to `nativeCode`; Task 03 must
  separate these meanings before UI formatting is wired.
- Queue values are point-in-time driver observations. They are diagnostics, not
  proof that a read/write completed or that cancellation settled.

### `SetCommTimeouts`

- On `FALSE`, capture `GetLastError()` immediately. On success, the settings have
  been replaced for that communications handle.
- Both write timeout fields being zero means no total write timeout. Preserve the
  Task 02 rule that the configured write timeout remains within 1..1000 ms.
- Timeout configuration is not terminal evidence. The result's typed timeout
  status and deadline evidence remain authoritative.

## Lean Evidence Shape

Reuse the existing `SerialOperationResult` identity instead of creating a second
parallel result model. It already carries request ID, generation, operation kind,
deadline, terminal status, deadline status, byte count, endpoint, category, and
native code.

Add only genuinely missing diagnostics:

- A stable direction enum for evidence rendering (`None`, `Tx`, `Rx`), derived
  once from operation kind. Do not store both direction and multiple boolean
  aliases.
- A distinct communications-error mask and optional in/out driver queue counts
  if `ClearCommError` diagnostics are exposed.
- A non-negative elapsed-milliseconds value if the log contract requires elapsed
  time. Saturate checked conversion rather than serializing a raw duration.

Do not serialize `steady_clock::time_point`; its epoch is process-local. Persist
deadline state plus an elapsed/remaining duration instead. Keep the full endpoint
for session operation, but clip the copy placed in a log/evidence entry. Define
field limits once in the log model and enforce them at entry construction; retain
the existing 4096-character rendered-line cap as the final defense.

Metadata evidence must not contain payload bytes. `SerialReadResult::bytes` and
queued write payloads are I/O data, not default diagnostic fields.

## UI And Log Wiring

- Consolidate typed result localization at the Win32 UI boundary. The existing
  `serialOperationErrorMessage`, `writeResultMessage`, and `readResultMessage`
  currently duplicate classification; one typed formatter should own the mapping.
- Known category/code combinations should map to concise Chinese guidance. A
  bounded system message may be appended as diagnostic detail, but is not the
  classification source.
- Extend `NativeLogEntry` with neutral scalar metadata, not Win32 types. A
  metadata-only constructor must leave `hasPayload == false` and `payload.empty()`.
- Preserve `nativeMakePayloadLogEntry` and raw-event persistence as the explicit
  payload paths. Do not automatically attach pending TX or RX bytes to transport
  status/error entries.
- Filtering, copying, and export should continue to operate on the rendered line.
  Sanitize each string field before rendering and clip the complete rendered line.
- `SVM_NATIVE_SELF_TEST_LOG` is a destination path, not evidence content. Do not
  echo it, serial payloads, or secrets into the log or job summary.

## CMake And CTest

Register tests with deterministic `add_test(NAME ... COMMAND ...)` names. The
test command's exit code is authoritative; `--output-on-failure` and saved logs
are diagnostics.

The Task 03 planned regex contains `transport_contract`, but the checked-in test
name is `serial_session_contract_tests`; that planned token does not select the
contract test. Use an exact focused selection and fail on an empty match:

```bash
ctest --test-dir <tree> --output-on-failure --no-tests=error \
  -R "^(serial_session_contract_tests|native_serial_io_state_tests|evidence_bundle_writer_tests|native_status_counters_state_tests|native_log_filter_state_tests)$"
```

Focused verification does not replace the full unfiltered host and MinGW test
trees. Host success also does not prove the Win32 implementation compiled.

Minimum deterministic checks:

- Every accepted terminal result retains request ID, generation, endpoint,
  deadline status, byte count, category, and native code where applicable.
- Timeout, cancellation, disconnect, short write, and native failure branch on
  typed fields and remain correct if display text changes.
- A communications-error mask is never formatted as a Win32 last-error code.
- Repeated metadata-only events contain no payload and remain bounded for long
  endpoint/system-message input.
- Repeated polling/export/filtering does not duplicate or mutate evidence.

Source review or an `rg` gate should additionally confirm that no serial caller
uses localized substrings for classification; this property cannot be proven by
changing a fake transport message alone.

## Wine And CI Evidence

- Use an isolated `WINEPREFIX` and a private `XDG_RUNTIME_DIR`. Propagate nonzero
  exits from both `wine ... --self-test` and `wine ... --ui-perf-test`; do not
  infer success only from a log file.
- Wine proves the application/adapter contract through Unix translation. It does
  not prove timing, unplug, cancellation, or error behavior of physical vendor
  USB-serial drivers.
- `GITHUB_STEP_SUMMARY` is human-readable diagnosis only. Keep it metadata-only
  and avoid serial payloads, secrets, and environment paths.
- Assert required evidence exists, is non-empty, and contains its expected gate
  marker before artifact upload. `if-no-files-found: error` remains a secondary
  guard, not the primary content check.
- Task 03 should preserve current CI evidence behavior. Broader workflow hardening
  and release-gate changes remain owned by Phase 3 Task 05.

## Exclusions

- No TCP, Qt, new runtime dependency, overlapped-I/O compatibility layer, HANDLE
  wrapper in neutral types, payload-by-default logging, string-based error
  classification, or duplicate status booleans.
- Do not treat `FormatMessageW`, CTest output regexes, Wine logs, step summaries,
  or artifact upload success as substitutes for typed results and process exits.
