# API Reference - Phase 3 Task 04: Dangerous Operation Confirmation and Audit

Generated: 2026-07-09T18:40:00+08:00

DeepWiki status: succeeded. No fallback used.

DeepWiki queries:
- Structure: `microsoft/Windows-classic-samples`
- Message box / dialog confirmation APIs: https://deepwiki.com/search/for-a-c-win32-native-desktop-a_7af40a15-9c35-4a01-b45f-5ee7aac9edf2
- Modal owner / return-code / cancellation patterns: https://deepwiki.com/search/in-windowsclassicsamples-what_49dc77a4-fdd2-487d-9392-1e28914064d5

## Scope

Task 04 requires explicit confirmation and audit evidence before dangerous operations:

- batch serial writes
- command-sequence writes
- broadcast Modbus writes
- dangerous Modbus register writes

The implementation must prevent silent execution after a failed, dismissed, or cancelled confirmation prompt. The research target is native Win32 explicit confirmation UI using Windows Classic Samples / Win32 API patterns.

## Win32 API Reference

| API / Pattern | Parameters / Flags | Return Values | Guidance for Task 04 |
| --- | --- | --- | --- |
| `MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType)` | `hWnd` should be the main window owner; `lpText` and `lpCaption` are UTF-16 strings; `uType` combines buttons, icon, default button, and modality. | Integer button result such as `IDOK`, `IDCANCEL`, `IDYES`, `IDNO`, `IDRETRY`. `0` indicates failure. | Use for simple dangerous-operation confirmation. Treat only the explicit affirmative result as authorization. Treat `IDCANCEL`, `IDNO`, close, escape, and `0` as deny/cancel. |
| `DialogBoxW` / `DialogBoxParamW` | Resource template, module handle, owner `HWND`, dialog proc, optional init parameter. | Returns the value passed to `EndDialog`, commonly `IDOK` or `IDCANCEL`; failure returns `-1`. | Use only if Task 04 needs richer detail or typed confirmation. For current scope, `MessageBoxW` is enough unless explicit text entry is required later. |
| `EndDialog(hwndDlg, result)` | Closes a modal dialog and sets the return code. | Caller receives `result` from `DialogBox*`. | Dialog cancel and window close handlers must call `EndDialog(IDCANCEL)` or equivalent deny result. |
| Owner `HWND` modality | `hWnd = window_` for this app's main window. | Owned modal UI blocks interaction with the owner until answered. | Always pass `window_` as owner from `NativeMainWindow`. Avoid `nullptr`, which can create an unowned prompt that may appear behind the app or fail to disable the relevant parent. |
| `MB_YESNO` | Shows Yes / No. | `IDYES` or `IDNO`. | Good for "Proceed with dangerous write?" prompts when no cancel third state is needed. |
| `MB_OKCANCEL` | Shows OK / Cancel. | `IDOK` or `IDCANCEL`. | Good for "Confirm operation" prompts. Prefer wording that makes OK unambiguous. |
| `MB_ICONWARNING` / `MB_EXCLAMATION` | Warning icon. | N/A | Use for operations that can modify device state. |
| `MB_DEFBUTTON2` | Second button is default. | N/A | Make the safer response default, for example No or Cancel. This reduces accidental Enter-key execution. |
| `MB_APPLMODAL` | Application-modal for owner chain. Usually default when owner is supplied. | N/A | Suitable default for app-local confirmation. |
| `MB_TASKMODAL` | Disables all top-level windows in the current task if no owner is supplied. | N/A | Use only as a fallback when no owner exists. Task 04 has `window_`, so owner modal is preferred. |
| `MB_SYSTEMMODAL` | System-wide modal behavior. | N/A | Avoid. Dangerous serial writes are app-local; system modal is disruptive. |

## Unicode and String Handling

- Use explicit `MessageBoxW` and wide strings (`std::wstring`, `LPCWSTR`) in native Win32 UI.
- Build prompt text in a local `std::wstring` that remains alive for the duration of the synchronous `MessageBoxW` call.
- Keep prompt text redaction-safe: include operation kind, byte count, Modbus slave id/function/address/count when useful; avoid raw payload contents unless already intentionally visible to the user.
- Existing native UI code already uses `MessageBoxW(window_, ..., MB_ICONWARNING | MB_OK)` in `main_window_modbus.cpp` for invalid scan parameters and uses `GetOpenFileNameW` with `hwndOwner = window_` in `main_window_send.cpp`; follow this owner and `W` API style.

## Return-Code Rules

Recommended helper semantics:

```text
confirmed = (MessageBoxW(...) == IDYES)      // for MB_YESNO
confirmed = (MessageBoxW(...) == IDOK)       // for MB_OKCANCEL
```

Everything else must be treated as not confirmed:

- `IDNO`
- `IDCANCEL`
- dialog close button
- Escape key
- `0` from `MessageBoxW` failure
- `-1` from `DialogBox*` failure if a custom dialog is later introduced

For dangerous operations, fail closed. A prompt failure is not permission to proceed.

## Local Integration Points

### Manual / quick / timed / file serial writes

Observed code paths:

- `NativeMainWindow::sendPayloadFromText(...)` validates connection and payload, then acquires `NativeSerialIoOwner::ManualSend`, then calls `enqueueManualSerialWrite(...)`.
- `NativeMainWindow::enqueueManualSerialWrite(...)` calls `serialPort_.enqueueWrite(...)` immediately.
- `NativeMainWindow::sendQuickPayload(...)` delegates to `sendPayloadFromText(...)`.
- `NativeMainWindow::startFileSend(...)` opens the file, acquires `NativeSerialIoOwner::FileSend`, starts `IDT_FILE_SEND`; `pumpFileSend()` then queues chunks through `enqueueFileSerialWrite(...)`.

Task 04 confirmation must happen before any operation reaches `serialPort_.enqueueWrite(...)`, before file-send timers are started, and before command-sequence execution enqueues writes. For cancelled confirmation, release any acquired serial owner and do not leave timers, pending writes, or partially opened file-send state active.

### Modbus scan / write path

Observed code paths:

- `NativeMainWindow::runModbusScan()` validates scan input, logs start, closes old scan thread, acquires `NativeSerialIoOwner::ModbusScan`, updates running UI, then creates `nativeModbusScanThreadProc`.
- Current scan request path appears read-focused (`nativeBuildModbusScanRequest`, supported read functions). Future write/broadcast support must gate dangerous write requests before acquisition/thread creation.
- Command sequence validation already rejects Modbus read broadcast id 0, but Task 04 explicitly covers broadcast Modbus writes and dangerous register writes, so the policy layer should classify write operations independently of current read-only scan assumptions.

Confirmation must occur after request classification is known and before `CreateThread` or direct transport writes. Broadcast id 0 and function codes commonly associated with writes (single/multiple coil/register writes such as 5, 6, 15, 16) should be denied without affirmative confirmation.

### Session evidence / audit

Observed structures:

- `SessionEvidenceEventType` currently includes `RawTx`, `RawRx`, `UserCommand`, `ModbusScanSettings`, `CommandSequenceStep`, `CommandSequenceAssertion`, `MatchResult`, `ReportMetadata`, `AppVersion`.
- `SessionEvidenceEvent::setMetadata(...)` supports public vs sensitive metadata.
- `SessionEvidenceSequencer::nextEvent(...)` creates ordered, timestamped events.

Task 04 should add an audit event type or reuse a clearly named user-command event only if schema churn must be minimized. Preferred schema is a distinct type such as `DangerousOperationConfirmation` so export/report code can identify confirmations and cancellations without parsing free text.

Audit metadata should include:

- `operation_kind` (`manual_write`, `file_send`, `command_sequence_write`, `modbus_broadcast_write`, `modbus_register_write`, etc.)
- `requires_confirmation` (`true`)
- `confirmation_result` (`confirmed`, `cancelled`, `failed`)
- `operation_summary` (redaction-safe)
- `byte_count`, `step_count`, `slave_id`, `function_code`, `address`, `quantity` where applicable
- `source_subsystem` (`win32_send`, `win32_modbus`, `command_sequence`)
- timestamp and order from `SessionEvidenceSequencer`

Do not store raw payload bytes in the confirmation metadata unless they are already classified as safe or sensitive metadata is properly marked.

## Common Failure Points

1. Ignoring `MessageBoxW` return values. This causes silent execution and defeats the task objective.
2. Treating only `IDCANCEL` as cancellation. With `MB_YESNO`, denial is `IDNO`; close behavior can also return a non-affirmative value.
3. Using `MB_DEFBUTTON1` for an affirmative first button. For dangerous writes, safer denial should be default (`MB_DEFBUTTON2` with `MB_YESNO` or `MB_OKCANCEL`).
4. Passing `nullptr` owner. An unowned prompt may be hidden behind the main window and may not block the operation context.
5. Prompting after acquisition/enqueue/thread creation. Confirmation must be before irreversible or externally visible work.
6. Recording only confirmed operations. Cancelled and failed prompts are also audit evidence because they prove the dangerous write did not execute silently.
7. Persisting raw payloads in audit metadata. Evidence must be useful but redaction-safe.

## Recommended Implementation Contract

Core policy should be independent of Win32 UI:

- classify an operation as safe or requiring confirmation
- produce a stable operation summary and metadata
- return a decision enum such as `NotRequired`, `Required`, `Confirmed`, `Cancelled`, `PromptFailed`

Win32 integration should be a thin adapter:

1. Build a `DangerousOperationRequest`.
2. Ask core policy whether confirmation is required.
3. If required, call `MessageBoxW(window_, text.c_str(), title.c_str(), MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2 | MB_APPLMODAL)`.
4. Convert only `IDYES` to confirmed.
5. Record audit evidence for confirmed, cancelled, and prompt failure.
6. Continue to enqueue/start only when confirmed or when policy says confirmation is not required.

## Test Guidance

- Unit test policy classification without Win32: safe manual single write, batch/file/sequence write, broadcast Modbus write, dangerous register write.
- Unit test return-code mapping: `IDYES`/`IDOK` confirms; `IDNO`, `IDCANCEL`, `0`, unknown values deny.
- Integration-level tests should assert cancelled dangerous operations do not call enqueue/write/thread-start adapters.
- Evidence tests should assert both confirmed and cancelled outcomes produce ordered, timestamped, redaction-safe metadata.
