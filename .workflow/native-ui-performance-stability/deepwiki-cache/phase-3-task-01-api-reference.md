# API Reference - Phase 3 Task 01: Serial Connection Regression

Generated: 2026-06-30T18:18:00+08:00

DeepWiki focused Task 01 queries against `microsoft/Windows-classic-samples` returned HTTP 500 twice. This cache therefore combines the successful phase-level DeepWiki result with Microsoft Learn official Win32 API documentation fallback.

| API | Library | Source | Confidence |
|---|---|---|---|
| `CreateFileW` | Win32 API | Microsoft Learn fallback | High |
| `GetCommState` / `SetCommState` / `DCB` | Win32 API | Microsoft Learn fallback | High |
| `SetCommTimeouts` / `COMMTIMEOUTS` | Win32 API | Microsoft Learn fallback | High |
| `ClearCommError` / `COMSTAT` | Win32 API | Microsoft Learn fallback | High |
| `ReadFile` / `WriteFile` | Win32 API | Microsoft Learn fallback + DeepWiki phase result | High |
| `CloseHandle` / `GetLastError` | Win32 API | Microsoft Learn fallback + DeepWiki phase result | High |

## `CreateFileW`

**Signature:** `HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)`

**Parameters used here:**

- `lpFileName`: Win32 device path such as `\\.\COM10`.
- `dwDesiredAccess`: `GENERIC_READ | GENERIC_WRITE`.
- `dwShareMode`: `0` for exclusive serial access.
- `dwCreationDisposition`: `OPEN_EXISTING`.
- `dwFlagsAndAttributes`: `FILE_ATTRIBUTE_NORMAL` for this synchronous backend.

**Returns:** valid `HANDLE` on success; `INVALID_HANDLE_VALUE` on failure.

**Errors:** call `GetLastError()` immediately after failure. Common serial cases are not found, access denied, sharing violation, invalid parameter, I/O device error, and device disconnected.

**Gotchas:**

- COM ports above `COM9` require the device path prefix.
- Synchronous handles can block; this project keeps UI usage bounded by short polling and explicit timeouts rather than long reads.

## `SetCommState` / `DCB`

**Signature:** `BOOL SetCommState(HANDLE hFile, LPDCB lpDCB)`

**Parameters used here:**

- `hFile`: serial handle returned by `CreateFileW`.
- `lpDCB`: `DCB` populated from current driver state via `GetCommState`, then updated for baud rate, byte size, parity, stop bits, DTR, RTS, and flow control.

**Returns:** nonzero on success, zero on failure.

**Errors:** call `GetLastError()` on failure. Invalid serial combinations commonly surface as invalid parameter or driver-specific errors.

**Gotchas:**

- Windows documents invalid `DCB` stop-bit combinations: 5 data bits cannot use 2 stop bits, and 6/7/8 data bits cannot use 1.5 stop bits.
- These combinations should be rejected in `validateSerialOpenOptions` before trying to open/configure the device.

## `SetCommTimeouts` / `COMMTIMEOUTS`

**Signature:** `BOOL SetCommTimeouts(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts)`

**Parameters used here:**

- `ReadIntervalTimeout = MAXDWORD`
- `ReadTotalTimeoutMultiplier = 0`
- `ReadTotalTimeoutConstant = readTimeoutMs`
- `WriteTotalTimeoutMultiplier = 0`
- `WriteTotalTimeoutConstant = writeTimeoutMs`

**Returns:** nonzero on success, zero on failure.

**Errors:** call `GetLastError()` on failure.

**Gotchas:**

- Negative timeout values must be rejected before conversion to `DWORD`.
- Timeout values define blocking behavior for synchronous `ReadFile`/`WriteFile`; UI-thread paths should not rely on long blocking waits.

## `ClearCommError` / `COMSTAT`

**Signature:** `BOOL ClearCommError(HANDLE hFile, LPDWORD lpErrors, LPCOMSTAT lpStat)`

**Parameters used here:**

- `lpErrors`: receives line/status error flags such as frame, overrun, parity, receive overflow, or break.
- `lpStat`: receives queue state, especially `cbInQue`.

**Returns:** nonzero on success, zero on failure.

**Errors:** call `GetLastError()` on failure. Nonzero line errors should be translated into actionable status text.

**Gotchas:**

- Checking `cbInQue` before `ReadFile` avoids a blocking read when no bytes are queued.
- Nonzero serial line errors are not ordinary "no data" cases and should close or report the connection failure.

## `ReadFile` / `WriteFile`

**Signatures:**

- `BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)`
- `BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)`

**Parameters used here:**

- synchronous serial handle;
- non-null buffer for nonzero length;
- chunk sizes clamped to `DWORD`;
- `lpOverlapped = nullptr`.

**Returns:** nonzero on success, zero on failure.

**Errors:** call `GetLastError()` on failure and translate to Chinese status text.

**Gotchas:**

- A zero-byte write result after a successful call is treated as a failed progress condition by this project.
- Long synchronous reads are avoided by `waitForReadyRead(0)` and `ClearCommError` queue checks before `readAvailable`.

## Task 01 Action

Task 01 should retain the existing synchronous backend but add regression coverage for invalid stop-bit/data-bit combinations, then verify native serial unit tests and PTY loopback reopen/transaction pressure.
