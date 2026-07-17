#include "win32/win32_serial_session.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

namespace svm::win32 {
namespace {

constexpr DWORD kSerialWorkerJoinBudgetMs =
    static_cast<DWORD>(svm::transport::kSerialTerminalResultTargetMs / 2);

HANDLE asHandle(void* handle) noexcept {
    return static_cast<HANDLE>(handle);
}

class WriteLock final {
public:
    explicit WriteLock(CRITICAL_SECTION& lock) noexcept
        : lock_(lock) {
        EnterCriticalSection(&lock_);
    }

    ~WriteLock() {
        LeaveCriticalSection(&lock_);
    }

    WriteLock(const WriteLock&) = delete;
    WriteLock& operator=(const WriteLock&) = delete;

private:
    CRITICAL_SECTION& lock_;
};

bool isValidHandle(HANDLE handle) noexcept {
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), required);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

BYTE parityToWin32(SerialParity parity) {
    switch (parity) {
    case SerialParity::None:
        return NOPARITY;
    case SerialParity::Odd:
        return ODDPARITY;
    case SerialParity::Even:
        return EVENPARITY;
    case SerialParity::Mark:
        return MARKPARITY;
    case SerialParity::Space:
        return SPACEPARITY;
    }
    return NOPARITY;
}

BYTE stopBitsToWin32(SerialStopBits stopBits) {
    switch (stopBits) {
    case SerialStopBits::One:
        return ONESTOPBIT;
    case SerialStopBits::OnePointFive:
        return ONE5STOPBITS;
    case SerialStopBits::Two:
        return TWOSTOPBITS;
    }
    return ONESTOPBIT;
}

bool unsupportedDisabledControlLine(unsigned long errorCode, bool enabled) noexcept {
    return !enabled && errorCode == ERROR_NOT_SUPPORTED;
}

svm::transport::SerialOperationStatus failureOperationStatus(
    svm::transport::SerialErrorCategory category) noexcept {
    switch (category) {
    case svm::transport::SerialErrorCategory::Timeout:
        return svm::transport::SerialOperationStatus::Timeout;
    case svm::transport::SerialErrorCategory::Cancelled:
    case svm::transport::SerialErrorCategory::SessionClosed:
        return svm::transport::SerialOperationStatus::Cancelled;
    case svm::transport::SerialErrorCategory::Disconnected:
        return svm::transport::SerialOperationStatus::Disconnected;
    case svm::transport::SerialErrorCategory::None:
    case svm::transport::SerialErrorCategory::InvalidInput:
    case svm::transport::SerialErrorCategory::QueueFull:
    case svm::transport::SerialErrorCategory::NativeFailure:
    case svm::transport::SerialErrorCategory::IoFailure:
        return svm::transport::SerialOperationStatus::Failed;
    }
    return svm::transport::SerialOperationStatus::Failed;
}

bool configureSerialTimeouts(
    HANDLE handle,
    int readTimeoutMs,
    int writeTimeoutMs,
    DWORD& nativeCode) {
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(std::max(0, readTimeoutMs));
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(std::clamp(
        writeTimeoutMs,
        1,
        svm::transport::kSerialTerminalResultTargetMs));
    if (!SetCommTimeouts(handle, &timeouts)) {
        nativeCode = GetLastError();
        return false;
    }
    return true;
}

int writeTimeoutForDeadline(
    const svm::transport::SerialDeadline& deadline,
    int configuredTimeoutMs) noexcept {
    const int boundedTimeoutMs = std::clamp(
        configuredTimeoutMs,
        1,
        svm::transport::kSerialTerminalResultTargetMs);
    if (!deadline.set()) {
        return boundedTimeoutMs;
    }
    const auto now = std::chrono::steady_clock::now();
    if (*deadline.expiresAt <= now) {
        return 1;
    }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
        *deadline.expiresAt - now);
    return static_cast<int>(std::clamp<std::int64_t>(
        remaining.count(),
        1,
        boundedTimeoutMs));
}

svm::transport::SerialDeadline boundedWriteDeadline(
    svm::transport::SerialDeadline deadline,
    int configuredTimeoutMs) noexcept {
    const auto configuredDeadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::clamp(
            configuredTimeoutMs,
            1,
            svm::transport::kSerialTerminalResultTargetMs));
    if (!deadline.set() || *deadline.expiresAt > configuredDeadline) {
        deadline.expiresAt = configuredDeadline;
    }
    return deadline;
}

bool configureSerialState(
    HANDLE handle,
    const SerialOpenOptions& options,
    DWORD& nativeCode) {
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle, &dcb)) {
        nativeCode = GetLastError();
        return false;
    }

    dcb.BaudRate = static_cast<DWORD>(options.baudRate);
    dcb.ByteSize = static_cast<BYTE>(options.dataBits);
    dcb.Parity = parityToWin32(options.parity);
    dcb.StopBits = stopBitsToWin32(options.stopBits);
    dcb.fBinary = TRUE;
    dcb.fParity = options.parity == SerialParity::None ? FALSE : TRUE;
    dcb.fOutxCtsFlow = options.flowControl == SerialFlowControl::HardwareRtsCts ? TRUE : FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = options.dataTerminalReady ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fTXContinueOnXoff = TRUE;
    dcb.fOutX = options.flowControl == SerialFlowControl::SoftwareXonXoff ? TRUE : FALSE;
    dcb.fInX = options.flowControl == SerialFlowControl::SoftwareXonXoff ? TRUE : FALSE;
    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;
    dcb.fRtsControl = options.flowControl == SerialFlowControl::HardwareRtsCts
        ? RTS_CONTROL_HANDSHAKE
        : (options.requestToSend ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE);
    dcb.fAbortOnError = FALSE;

    if (!SetCommState(handle, &dcb)) {
        nativeCode = GetLastError();
        return false;
    }

    if (!configureSerialTimeouts(
        handle,
        options.readTimeoutMs,
        options.writeTimeoutMs,
        nativeCode)) {
        return false;
    }

    return true;
}

bool applyControlLines(
    HANDLE handle,
    const SerialOpenOptions& options,
    DWORD& nativeCode) {
    if (!EscapeCommFunction(handle, options.dataTerminalReady ? SETDTR : CLRDTR)) {
        nativeCode = GetLastError();
        if (!unsupportedDisabledControlLine(nativeCode, options.dataTerminalReady)) {
            return false;
        }
        nativeCode = 0;
    }

    if (options.flowControl != SerialFlowControl::HardwareRtsCts
        && !EscapeCommFunction(handle, options.requestToSend ? SETRTS : CLRRTS)) {
        nativeCode = GetLastError();
        if (!unsupportedDisabledControlLine(nativeCode, options.requestToSend)) {
            return false;
        }
        nativeCode = 0;
    }

    return true;
}

} // namespace

Win32SerialSession::Win32SerialSession() {
    InitializeCriticalSection(&lifecycleLock_);
    InitializeCriticalSection(&writeLock_);
    InitializeCriticalSection(&ioLock_);
}

Win32SerialSession::~Win32SerialSession() {
    close();
    if (writeThread_ != nullptr
        || writeWakeEvent_ != nullptr
        || isValidHandle(asHandle(handle_))) {
        std::terminate();
    }
    DeleteCriticalSection(&ioLock_);
    DeleteCriticalSection(&writeLock_);
    DeleteCriticalSection(&lifecycleLock_);
}

svm::transport::SerialOperationResult Win32SerialSession::open(SerialOpenOptions options) {
    WriteLock lifecycleLock(lifecycleLock_);
    const std::string requestedEndpoint = options.portName;
    if (snapshot().state != svm::transport::SerialSessionState::Closed) {
        const svm::transport::SerialOperationResult closeResult = close();
        if (!closeResult.succeeded()) {
            return operationResult(
                svm::transport::SerialOperationKind::Open,
                failureOperationStatus(closeResult.error.category),
                svm::transport::kUnassignedSerialSessionGeneration,
                requestedEndpoint,
                {},
                0,
                closeResult.error.category,
                closeResult.error.nativeCode,
                allocateOperationId());
        }
    }

    const svm::transport::SerialOperationId operationId = allocateOperationId();
    {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Opening;
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
    }

    const SerialValidationResult validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Faulted;
        return operationResult(
            svm::transport::SerialOperationKind::Open,
            svm::transport::SerialOperationStatus::RejectedInvalid,
            generation_,
            requestedEndpoint,
            {},
            0,
            svm::transport::SerialErrorCategory::InvalidInput,
            0,
            operationId);
    }

    const std::string devicePath = makeWin32DevicePath(options.portName);
    const std::wstring wideDevicePath = utf8ToWide(devicePath);
    DWORD nativeCode = 0;
    HANDLE handle = nullptr;
    {
        WriteLock ioLock(ioLock_);
        handle = CreateFileW(
            wideDevicePath.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (!isValidHandle(handle)) {
            nativeCode = GetLastError();
        } else if (!SetupComm(handle, static_cast<DWORD>(options.readBufferSize), 4096)) {
            nativeCode = GetLastError();
        } else if (!PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR)) {
            nativeCode = GetLastError();
        } else if (!configureSerialState(handle, options, nativeCode)
            || !applyControlLines(handle, options, nativeCode)) {
        } else {
            handle_ = handle;
        }

        if (nativeCode != 0 && isValidHandle(handle)) {
            if (CloseHandle(handle)) {
                handle = nullptr;
            } else {
                handle_ = handle;
            }
        }
    }

    if (nativeCode != 0 || !isValidHandle(handle)) {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Faulted;
        const svm::transport::SerialErrorCategory category = nativeErrorCategory(nativeCode, false);
        return operationResult(
            svm::transport::SerialOperationKind::Open,
            failureOperationStatus(category),
            generation_,
            requestedEndpoint,
            {},
            0,
            category,
            nativeCode,
            operationId);
    }

    {
        WriteLock lock(writeLock_);
        if (generationCounter_ != std::numeric_limits<svm::transport::SerialSessionGeneration>::max()) {
            ++generationCounter_;
            if (writeQueue_.beginGeneration(generationCounter_)) {
                generation_ = generationCounter_;
                options_ = std::move(options);
                options_.portName = stripWin32DevicePrefix(devicePath);
                state_ = svm::transport::SerialSessionState::Open;
                return operationResult(
                    svm::transport::SerialOperationKind::Open,
                    svm::transport::SerialOperationStatus::Succeeded,
                    generation_,
                    options_.portName,
                    {},
                    0,
                    svm::transport::SerialErrorCategory::None,
                    0,
                    operationId);
            }
        }
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
        state_ = svm::transport::SerialSessionState::Faulted;
    }
    DWORD closeNativeCode = 0;
    {
        WriteLock ioLock(ioLock_);
        if (CloseHandle(handle)) {
            handle_ = nullptr;
        } else {
            closeNativeCode = GetLastError();
            handle_ = handle;
        }
    }
    {
        WriteLock lock(writeLock_);
        return operationResult(
            svm::transport::SerialOperationKind::Open,
            svm::transport::SerialOperationStatus::Failed,
            generation_,
            requestedEndpoint,
            {},
            0,
            closeNativeCode == 0
                ? svm::transport::SerialErrorCategory::IoFailure
                : svm::transport::SerialErrorCategory::NativeFailure,
            closeNativeCode,
            operationId);
    }
}

svm::transport::SerialOperationResult Win32SerialSession::close() {
    WriteLock lifecycleLock(lifecycleLock_);
    svm::transport::SerialSessionGeneration closingGeneration = 0;
    std::string closingEndpoint;
    {
        WriteLock lock(writeLock_);
        if (state_ == svm::transport::SerialSessionState::Closed) {
            const svm::transport::SerialSessionSnapshot snapshot{
                .state = state_,
                .generation = generation_,
                .endpoint = options_.portName,
                .options = options_,
            };
            return rejectedClosed(svm::transport::SerialOperationKind::Close, snapshot);
        }
        state_ = svm::transport::SerialSessionState::Closing;
        closingGeneration = generation_;
        closingEndpoint = options_.portName;
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
    }

    const svm::transport::SerialOperationId operationId = allocateOperationId();
    const WorkerStopOutcome workerStop = stopWriteWorker();
    if (!workerStop.joined) {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Faulted;
        return operationResult(
            svm::transport::SerialOperationKind::Close,
            svm::transport::SerialOperationStatus::Failed,
            closingGeneration,
            closingEndpoint,
            {},
            0,
            svm::transport::SerialErrorCategory::NativeFailure,
            workerStop.nativeCode,
            operationId);
    }

    DWORD closeNativeCode = 0;
    {
        WriteLock ioLock(ioLock_);
        HANDLE handle = asHandle(handle_);
        if (isValidHandle(handle)) {
            if (!CloseHandle(handle)) {
                closeNativeCode = GetLastError();
            } else {
                handle_ = nullptr;
            }
        } else {
            handle_ = nullptr;
        }
    }

    WriteLock lock(writeLock_);
    const DWORD nativeCode = closeNativeCode != 0 ? closeNativeCode : workerStop.nativeCode;
    if (!workerStop.threadHandleClosed
        || !workerStop.wakeEventHandleClosed
        || closeNativeCode != 0
        || nativeCode != 0) {
        state_ = svm::transport::SerialSessionState::Faulted;
        return operationResult(
            svm::transport::SerialOperationKind::Close,
            svm::transport::SerialOperationStatus::Failed,
            closingGeneration,
            std::move(closingEndpoint),
            {},
            0,
            svm::transport::SerialErrorCategory::NativeFailure,
            nativeCode,
            operationId);
    }
    options_ = {};
    state_ = svm::transport::SerialSessionState::Closed;
    return operationResult(
        svm::transport::SerialOperationKind::Close,
        svm::transport::SerialOperationStatus::Succeeded,
        closingGeneration,
        std::move(closingEndpoint),
        {},
        0,
        svm::transport::SerialErrorCategory::None,
        0,
        operationId);
}

svm::transport::SerialSessionSnapshot Win32SerialSession::snapshot() const {
    WriteLock lock(writeLock_);
    return sessionSnapshotLocked();
}

svm::transport::SerialByteStream& Win32SerialSession::byteStream() noexcept {
    return *this;
}

svm::transport::SerialWriteScheduler& Win32SerialSession::writeScheduler() noexcept {
    return *this;
}

svm::transport::SerialSessionSnapshot Win32SerialSession::sessionSnapshotLocked() const {
    return {
        .state = state_,
        .generation = generation_,
        .endpoint = options_.portName,
        .options = options_,
    };
}

svm::transport::SerialOperationResult
Win32SerialSession::setDataTerminalReady(bool enabled) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            return rejectedClosed(
                svm::transport::SerialOperationKind::SetDataTerminalReady,
                snapshot);
        }
    }
    const auto operationId = allocateOperationId();

    DWORD nativeCode = 0;
    bool leased = false;
    bool completedCurrent = false;
    {
        WriteLock ioLock(ioLock_);
        const HANDLE handle = validatedHandleForGeneration(snapshot.generation);
        leased = isValidHandle(handle);
        if (leased && !EscapeCommFunction(handle, enabled ? SETDTR : CLRDTR)) {
            nativeCode = GetLastError();
        }
        completedCurrent = leased
            && validatedHandleForGeneration(snapshot.generation) == handle;
    }

    if (!completedCurrent) {
        return operationResult(
            svm::transport::SerialOperationKind::SetDataTerminalReady,
            svm::transport::SerialOperationStatus::Cancelled,
            snapshot.generation,
            snapshot.endpoint,
            {},
            0,
            svm::transport::SerialErrorCategory::SessionClosed,
            0,
            operationId);
    }

    const auto category = nativeCode == 0
        ? svm::transport::SerialErrorCategory::None
        : nativeErrorCategory(nativeCode, false);
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(snapshot.generation);
    }
    {
        WriteLock lock(writeLock_);
        if (nativeCode == 0
            && state_ == svm::transport::SerialSessionState::Open
            && generation_ == snapshot.generation) {
            options_.dataTerminalReady = enabled;
        }
    }
    if (nativeCode != 0) {
        return operationResult(
            svm::transport::SerialOperationKind::SetDataTerminalReady,
            failureOperationStatus(category),
            snapshot.generation,
            snapshot.endpoint,
            {},
            0,
            category,
            nativeCode,
            operationId);
    }
    return operationResult(
        svm::transport::SerialOperationKind::SetDataTerminalReady,
        svm::transport::SerialOperationStatus::Succeeded,
        snapshot.generation,
        snapshot.endpoint,
        {},
        0,
        svm::transport::SerialErrorCategory::None,
        0,
        operationId);
}

svm::transport::SerialOperationResult
Win32SerialSession::setRequestToSend(bool enabled) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            return rejectedClosed(svm::transport::SerialOperationKind::SetRequestToSend, snapshot);
        }
        if (snapshot.usesHardwareRtsCts()) {
            return operationResult(
                svm::transport::SerialOperationKind::SetRequestToSend,
                svm::transport::SerialOperationStatus::RejectedInvalid,
                snapshot.generation,
                snapshot.endpoint,
                {},
                0,
                svm::transport::SerialErrorCategory::InvalidInput,
                0,
                svm::transport::kUnassignedSerialOperationId);
        }
    }
    const auto operationId = allocateOperationId();

    DWORD nativeCode = 0;
    bool leased = false;
    bool completedCurrent = false;
    {
        WriteLock ioLock(ioLock_);
        const HANDLE handle = validatedHandleForGeneration(snapshot.generation);
        leased = isValidHandle(handle);
        if (leased && !EscapeCommFunction(handle, enabled ? SETRTS : CLRRTS)) {
            nativeCode = GetLastError();
        }
        completedCurrent = leased
            && validatedHandleForGeneration(snapshot.generation) == handle;
    }

    if (!completedCurrent) {
        return operationResult(
            svm::transport::SerialOperationKind::SetRequestToSend,
            svm::transport::SerialOperationStatus::Cancelled,
            snapshot.generation,
            snapshot.endpoint,
            {},
            0,
            svm::transport::SerialErrorCategory::SessionClosed,
            0,
            operationId);
    }

    const auto category = nativeCode == 0
        ? svm::transport::SerialErrorCategory::None
        : nativeErrorCategory(nativeCode, false);
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(snapshot.generation);
    }
    {
        WriteLock lock(writeLock_);
        if (nativeCode == 0
            && state_ == svm::transport::SerialSessionState::Open
            && generation_ == snapshot.generation) {
            options_.requestToSend = enabled;
        }
    }
    if (nativeCode != 0) {
        return operationResult(
            svm::transport::SerialOperationKind::SetRequestToSend,
            failureOperationStatus(category),
            snapshot.generation,
            snapshot.endpoint,
            {},
            0,
            category,
            nativeCode,
            operationId);
    }
    return operationResult(
        svm::transport::SerialOperationKind::SetRequestToSend,
        svm::transport::SerialOperationStatus::Succeeded,
        snapshot.generation,
        snapshot.endpoint,
        {},
        0,
        svm::transport::SerialErrorCategory::None,
        0,
        operationId);
}

Win32SerialSession::NativeIoOutcome Win32SerialSession::writeBytesInternal(
    const std::uint8_t* payload,
    std::size_t size,
    svm::transport::SerialSessionGeneration expectedGeneration,
    svm::transport::SerialDeadline deadline) {
    svm::transport::SerialSessionGeneration generation = expectedGeneration;
    SerialOpenOptions options;
    {
        WriteLock lock(writeLock_);
        if (state_ != svm::transport::SerialSessionState::Open
            || (generation != svm::transport::kUnassignedSerialSessionGeneration
                && generation_ != generation)) {
            NativeIoOutcome outcome{
                .ok = false,
                .category = svm::transport::SerialErrorCategory::SessionClosed,
            };
            return outcome;
        }
        generation = generation_;
        options = options_;
    }
    if (size > 0 && payload == nullptr) {
        NativeIoOutcome outcome{
            .ok = false,
            .category = svm::transport::SerialErrorCategory::InvalidInput,
        };
        return outcome;
    }

    NativeIoOutcome outcome;
    {
        WriteLock ioLock(ioLock_);
        const HANDLE handle = validatedHandleForGeneration(generation);
        outcome = isValidHandle(handle)
            ? writeBytesToHandle(
                  handle,
                  payload,
                  size,
                  deadline,
                  options.readTimeoutMs,
                  options.writeTimeoutMs)
            : NativeIoOutcome{
                  .ok = false,
                  .category = svm::transport::SerialErrorCategory::SessionClosed,
              };
        if (isValidHandle(handle) && validatedHandleForGeneration(generation) != handle) {
            outcome.ok = false;
            outcome.category = svm::transport::SerialErrorCategory::SessionClosed;
        }
    }
    if (outcome.category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(generation);
    }
    return outcome;
}

Win32SerialSession::NativeIoOutcome Win32SerialSession::writeBytesToHandle(
    HANDLE handle,
    const std::uint8_t* payload,
    std::size_t size,
    svm::transport::SerialDeadline deadline,
    int readTimeoutMs,
    int writeTimeoutMs) {
    if (size == 0) {
        return {.ok = true};
    }

    std::size_t totalWritten = 0;
    while (totalWritten < size) {
        if (deadlineExpired(deadline)) {
            return {
                .ok = false,
                .byteCount = totalWritten,
                .category = svm::transport::SerialErrorCategory::Timeout,
            };
        }
        DWORD timeoutNativeCode = 0;
        if (!configureSerialTimeouts(
                handle,
                readTimeoutMs,
                writeTimeoutForDeadline(deadline, writeTimeoutMs),
                timeoutNativeCode)) {
            return {
                .ok = false,
                .byteCount = totalWritten,
                .nativeCode = timeoutNativeCode,
                .category = nativeErrorCategory(timeoutNativeCode, false),
            };
        }
        const std::size_t remaining = size - totalWritten;
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, payload + totalWritten, chunkSize, &written, nullptr)) {
            const DWORD nativeCode = GetLastError();
            return {
                .ok = false,
                .byteCount = totalWritten,
                .nativeCode = nativeCode,
                .category = nativeErrorCategory(nativeCode, false),
            };
        }

        totalWritten += written;
        if (written == 0) {
            return {
                .ok = false,
                .byteCount = totalWritten,
                .category = svm::transport::SerialErrorCategory::Timeout,
            };
        }
    }

    return {
        .ok = true,
        .byteCount = totalWritten,
    };
}

HANDLE Win32SerialSession::validatedHandleForGeneration(
    svm::transport::SerialSessionGeneration generation) const noexcept {
    WriteLock lock(writeLock_);
    if (state_ != svm::transport::SerialSessionState::Open || generation_ != generation) {
        return nullptr;
    }
    const HANDLE handle = asHandle(handle_);
    return isValidHandle(handle) ? handle : nullptr;
}

svm::transport::SerialTerminalResult Win32SerialSession::writeBytes(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    const auto snapshot = this->snapshot();
    if (!snapshot.open()) {
        return rejectedClosed(svm::transport::SerialOperationKind::Write, snapshot, deadline);
    }
    if (payload.empty()) {
        return operationResult(
            svm::transport::SerialOperationKind::Write,
            svm::transport::SerialOperationStatus::RejectedInvalid,
            snapshot.generation,
            snapshot.endpoint,
            deadline,
            0,
            svm::transport::SerialErrorCategory::InvalidInput);
    }
    deadline = boundedWriteDeadline(deadline, snapshot.options.writeTimeoutMs);
    std::optional<svm::transport::SerialWriteRequestId> requestId;
    {
        WriteLock lock(writeLock_);
        requestId = writeQueue_.reserveRequestId();
    }
    if (!requestId.has_value()) {
        return operationResult(
            svm::transport::SerialOperationKind::Write,
            svm::transport::SerialOperationStatus::Failed,
            snapshot.generation,
            snapshot.endpoint,
            deadline,
            0,
            svm::transport::SerialErrorCategory::IoFailure);
    }
    if (deadlineExpired(deadline)) {
        return operationResult(
            svm::transport::SerialOperationKind::Write,
            svm::transport::SerialOperationStatus::Timeout,
            snapshot.generation,
            snapshot.endpoint,
            deadline,
            0,
            svm::transport::SerialErrorCategory::Timeout,
            0,
            *requestId);
    }

    NativeIoOutcome outcome = writeBytesInternal(
        payload.data(),
        payload.size(),
        snapshot.generation,
        deadline);
    svm::transport::SerialOperationStatus status = svm::transport::SerialOperationStatus::Succeeded;
    svm::transport::SerialErrorCategory category = outcome.category;
    if (!outcome.ok) {
        status = failureOperationStatus(outcome.category);
    } else if (deadlineExpired(deadline)) {
        status = svm::transport::SerialOperationStatus::Timeout;
        category = svm::transport::SerialErrorCategory::Timeout;
    }
    return operationResult(
        svm::transport::SerialOperationKind::Write,
        status,
        snapshot.generation,
        snapshot.endpoint,
        deadline,
        outcome.byteCount,
        category,
        outcome.nativeCode,
        *requestId);
}

Win32SerialSession::WriteAdmission Win32SerialSession::enqueueWriteCore(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    WriteAdmission admission;
    {
        WriteLock lock(writeLock_);
        admission.endpoint = options_.portName;
        if (state_ != svm::transport::SerialSessionState::Open) {
            admission.result = {
                .generation = generation_,
                .status = svm::transport::SerialWriteResultStatus::Failed,
                .deadline = deadline,
            };
            admission.status = svm::transport::SerialOperationStatus::RejectedClosed;
            admission.category = svm::transport::SerialErrorCategory::SessionClosed;
            return admission;
        }

        deadline = boundedWriteDeadline(deadline, options_.writeTimeoutMs);
        admission.result = writeQueue_.enqueue(
            std::move(payload),
            std::max(1, options_.writeTimeoutMs),
            generation_,
            deadline);
        const auto failAdmission = [&](std::uint32_t nativeCode) {
            writeQueue_.cancelPending(admission.result.requestId);
            admission.result = {
                .requestId = admission.result.requestId,
                .generation = admission.result.generation,
                .status = svm::transport::SerialWriteResultStatus::Failed,
                .byteCount = 0,
                .deadline = admission.result.deadline,
            };
            admission.category = svm::transport::SerialErrorCategory::NativeFailure;
            admission.status = svm::transport::SerialOperationStatus::Failed;
            admission.nativeCode = nativeCode;
        };
        std::uint32_t nativeCode = 0;
        if (admission.result.accepted() && !ensureWriteWorkerLocked(nativeCode)) {
            failAdmission(nativeCode);
        } else if (admission.result.accepted() && !SetEvent(writeWakeEvent_)) {
            nativeCode = GetLastError();
            failAdmission(nativeCode);
        } else if (admission.result.accepted()) {
            admission.status = svm::transport::SerialOperationStatus::Accepted;
            admission.category = svm::transport::SerialErrorCategory::None;
        } else if (admission.result.status == svm::transport::SerialWriteResultStatus::RejectedFull) {
            admission.status = svm::transport::SerialOperationStatus::RejectedFull;
            admission.category = svm::transport::SerialErrorCategory::QueueFull;
        } else {
            admission.status = svm::transport::SerialOperationStatus::RejectedInvalid;
            admission.category = svm::transport::SerialErrorCategory::InvalidInput;
        }
    }
    return admission;
}

svm::transport::SerialWriteAdmissionResult Win32SerialSession::enqueueWrite(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    const WriteAdmission admission = enqueueWriteCore(std::move(payload), deadline);
    return operationResult(
        svm::transport::SerialOperationKind::Write,
        admission.status,
        admission.result.generation,
        admission.endpoint,
        admission.result.deadline,
        admission.result.byteCount,
        admission.category,
        admission.nativeCode,
        admission.result.requestId);
}

std::vector<svm::transport::SerialTerminalResult>
Win32SerialSession::cancelPendingWrites() {
    std::vector<svm::transport::SerialTerminalResult> results;
    {
        WriteLock lock(writeLock_);
        const std::string currentEndpoint = options_.portName;
        for (const auto& cancelled : writeQueue_.cancelAllPending()) {
            results.push_back(terminalResult(
                cancelled,
                svm::transport::SerialOperationStatus::Cancelled,
                svm::transport::SerialErrorCategory::Cancelled,
                currentEndpoint));
        }
        if (writeWakeEvent_ != nullptr) {
            SetEvent(writeWakeEvent_);
        }
    }
    return results;
}

std::vector<svm::transport::SerialTerminalResult>
Win32SerialSession::takeCompletedWrites() {
    WriteLock lock(writeLock_);
    std::vector<svm::transport::SerialTerminalResult> results;
    results.reserve(completedWrites_.size());
    while (!completedWrites_.empty()) {
        results.push_back(std::move(completedWrites_.front()));
        completedWrites_.pop_front();
    }
    return results;
}

svm::transport::SerialWriteQueueSnapshot Win32SerialSession::writeQueueSnapshot() const {
    WriteLock lock(writeLock_);
    return writeQueue_.snapshot();
}

bool Win32SerialSession::ensureWriteWorkerLocked(std::uint32_t& nativeCode) {
    nativeCode = 0;
    if (writeThread_ != nullptr) {
        const DWORD waitResult = WaitForSingleObject(writeThread_, 0);
        if (waitResult == WAIT_TIMEOUT) {
            return true;
        }
        if (waitResult == WAIT_FAILED) {
            nativeCode = GetLastError();
            return false;
        }
        if (!CloseHandle(writeThread_)) {
            nativeCode = GetLastError();
            return false;
        }
        writeThread_ = nullptr;
        if (writeWakeEvent_ != nullptr) {
            if (!CloseHandle(writeWakeEvent_)) {
                nativeCode = GetLastError();
                return false;
            }
            writeWakeEvent_ = nullptr;
        }
    }
    if (writeWakeEvent_ == nullptr) {
        writeWakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (writeWakeEvent_ == nullptr) {
            nativeCode = GetLastError();
            return false;
        }
    }
    writeWorkerStopRequested_ = false;
    writeThread_ = CreateThread(nullptr, 0, &Win32SerialSession::writeWorkerThreadProc, this, 0, nullptr);
    if (writeThread_ == nullptr) {
        nativeCode = GetLastError();
        return false;
    }
    return true;
}

void Win32SerialSession::settlePendingWritesLocked(
    svm::transport::SerialErrorCategory category,
    std::uint32_t nativeCode) {
    svm::transport::SerialOperationStatus status = svm::transport::SerialOperationStatus::Failed;
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        status = svm::transport::SerialOperationStatus::Disconnected;
    } else if (category == svm::transport::SerialErrorCategory::Cancelled
        || category == svm::transport::SerialErrorCategory::SessionClosed) {
        status = svm::transport::SerialOperationStatus::Cancelled;
    }
    const std::string currentEndpoint = options_.portName;
    for (const auto& cancelled : writeQueue_.cancelAllPending()) {
        publishCompletion(terminalResult(cancelled, status, category, currentEndpoint, nativeCode));
    }
}

void Win32SerialSession::markGenerationDisconnected(
    svm::transport::SerialSessionGeneration generation) {
    {
        WriteLock lock(writeLock_);
        if (state_ != svm::transport::SerialSessionState::Open || generation_ != generation) {
            return;
        }
        state_ = svm::transport::SerialSessionState::Faulted;
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
        writeWorkerStopRequested_ = true;
        if (activeWrite_.has_value()) {
            activeCancellationCategory_ = svm::transport::SerialErrorCategory::Disconnected;
        }
        settlePendingWritesLocked(svm::transport::SerialErrorCategory::Disconnected);
        if (writeWakeEvent_ != nullptr) {
            SetEvent(writeWakeEvent_);
        }
    }
}

Win32SerialSession::WorkerStopOutcome Win32SerialSession::stopWriteWorker(
    svm::transport::SerialErrorCategory category) {
    WorkerStopOutcome outcome;
    HANDLE thread = nullptr;
    HANDLE handle = nullptr;
    svm::transport::SerialErrorCategory activeCategory = category;
    {
        WriteLock lock(writeLock_);
        writeWorkerStopRequested_ = true;
        if (activeWrite_.has_value()) {
            if (activeCancellationCategory_ == svm::transport::SerialErrorCategory::None) {
                activeCancellationCategory_ = category;
            }
            activeCategory = activeCancellationCategory_;
        } else {
            activeCancellationCategory_ = svm::transport::SerialErrorCategory::None;
        }
        thread = writeThread_;
        handle = asHandle(handle_);
        settlePendingWritesLocked(category);
    }
    if (thread != nullptr) {
        DWORD waitResult = WaitForSingleObject(thread, 0);
        if (waitResult == WAIT_TIMEOUT) {
            DWORD cancellationNativeCode = 0;
            if (writeWakeEvent_ == nullptr || !SetEvent(writeWakeEvent_)) {
                cancellationNativeCode = writeWakeEvent_ == nullptr
                    ? ERROR_INVALID_HANDLE
                    : GetLastError();
            }
            if (!CancelSynchronousIo(thread)) {
                const DWORD nativeCode = GetLastError();
                if (nativeCode != ERROR_NOT_FOUND && cancellationNativeCode == 0) {
                    cancellationNativeCode = nativeCode;
                }
            }
            if (isValidHandle(handle) && !PurgeComm(handle, PURGE_TXABORT | PURGE_RXABORT)) {
                const DWORD nativeCode = GetLastError();
                if (cancellationNativeCode == 0) {
                    cancellationNativeCode = nativeCode;
                }
            }
            waitResult = WaitForSingleObject(
                thread,
                kSerialWorkerJoinBudgetMs);
            if (waitResult == WAIT_TIMEOUT) {
                outcome.nativeCode = ERROR_TIMEOUT;
            } else if (waitResult != WAIT_OBJECT_0 && cancellationNativeCode != 0) {
                outcome.nativeCode = cancellationNativeCode;
            }
        }
        if (waitResult != WAIT_OBJECT_0) {
            outcome.joined = false;
            outcome.threadHandleClosed = false;
            if (waitResult == WAIT_FAILED) {
                outcome.nativeCode = GetLastError();
            } else if (outcome.nativeCode == 0) {
                outcome.nativeCode = ERROR_GEN_FAILURE;
            }
            {
                WriteLock lock(writeLock_);
                publishInterruptedActiveWriteLocked(activeCategory, outcome.nativeCode);
            }
            return outcome;
        }
        if (!CloseHandle(thread)) {
            const DWORD nativeCode = GetLastError();
            outcome.threadHandleClosed = false;
            outcome.nativeCode = nativeCode;
        }
    }
    {
        WriteLock lock(writeLock_);
        if (activeWrite_.has_value()) {
            const bool disconnected = activeCategory == svm::transport::SerialErrorCategory::Disconnected;
            finalizeActiveWriteLocked(
                *activeWrite_,
                disconnected
                    ? svm::transport::SerialWriteResultStatus::Disconnected
                    : svm::transport::SerialWriteResultStatus::Closed,
                0,
                activeCategory,
                0);
        }
        if (outcome.threadHandleClosed && writeThread_ == thread) {
            writeThread_ = nullptr;
        }
        writeWorkerStopRequested_ = false;
        activeCancellationCategory_ = svm::transport::SerialErrorCategory::None;
        if (writeWakeEvent_ != nullptr) {
            if (CloseHandle(writeWakeEvent_)) {
                writeWakeEvent_ = nullptr;
            } else {
                outcome.wakeEventHandleClosed = false;
                outcome.nativeCode = GetLastError();
            }
        }
    }
    return outcome;
}

void Win32SerialSession::writeWorkerLoop() {
    while (true) {
        std::optional<svm::transport::SerialWriteRequest> request;
        while (true) {
            {
                WriteLock lock(writeLock_);
                if (writeWorkerStopRequested_) {
                    return;
                }
                if (writeQueue_.pendingCount() > 0) {
                    request = writeQueue_.activateNext();
                    if (request.has_value()) {
                        activeWrite_ = ActiveWrite{
                            .requestId = request->id,
                            .generation = request->generation,
                            .deadline = request->deadline,
                            .payloadBytes = request->payloadBytes,
                            .endpoint = options_.portName,
                        };
                        activeCancellationCategory_ = svm::transport::SerialErrorCategory::None;
                    }
                    break;
                }
            }
            const DWORD waitResult = WaitForSingleObject(writeWakeEvent_, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                const DWORD nativeCode = waitResult == WAIT_FAILED
                    ? GetLastError()
                    : ERROR_GEN_FAILURE;
                WriteLock lock(writeLock_);
                if (!writeWorkerStopRequested_) {
                    state_ = svm::transport::SerialSessionState::Faulted;
                    generation_ = svm::transport::kUnassignedSerialSessionGeneration;
                    writeWorkerStopRequested_ = true;
                    settlePendingWritesLocked(
                        svm::transport::SerialErrorCategory::NativeFailure,
                        nativeCode);
                }
                return;
            }
        }

        if (!request.has_value()) {
            continue;
        }

        NativeIoOutcome outcome;
        if (deadlineExpired(request->deadline)) {
            outcome = {
                .ok = false,
                .category = svm::transport::SerialErrorCategory::Timeout,
            };
        } else {
            outcome = writeBytesInternal(
                request->payload.data(),
                request->payload.size(),
                request->generation,
                request->deadline);
        }

        {
            WriteLock lock(writeLock_);
            svm::transport::SerialWriteResultStatus status = svm::transport::SerialWriteResultStatus::Sent;
            svm::transport::SerialErrorCategory category = outcome.category;
            if (writeWorkerStopRequested_
                || activeCancellationCategory_ != svm::transport::SerialErrorCategory::None) {
                category = activeCancellationCategory_ == svm::transport::SerialErrorCategory::None
                    ? svm::transport::SerialErrorCategory::SessionClosed
                    : activeCancellationCategory_;
                status = category == svm::transport::SerialErrorCategory::Disconnected
                    ? svm::transport::SerialWriteResultStatus::Disconnected
                    : svm::transport::SerialWriteResultStatus::Closed;
            } else if (!outcome.ok) {
                status = outcome.category == svm::transport::SerialErrorCategory::Timeout
                    ? svm::transport::SerialWriteResultStatus::Timeout
                    : (outcome.category == svm::transport::SerialErrorCategory::Disconnected
                          ? svm::transport::SerialWriteResultStatus::Disconnected
                          : (outcome.category == svm::transport::SerialErrorCategory::Cancelled
                                ? svm::transport::SerialWriteResultStatus::Cancelled
                                : svm::transport::SerialWriteResultStatus::Failed));
            } else if (deadlineExpired(request->deadline)) {
                status = svm::transport::SerialWriteResultStatus::Timeout;
                category = svm::transport::SerialErrorCategory::Timeout;
            }
            if (category == svm::transport::SerialErrorCategory::Disconnected
                && state_ == svm::transport::SerialSessionState::Open
                && generation_ == request->generation) {
                state_ = svm::transport::SerialSessionState::Faulted;
                generation_ = svm::transport::kUnassignedSerialSessionGeneration;
                writeWorkerStopRequested_ = true;
                settlePendingWritesLocked(svm::transport::SerialErrorCategory::Disconnected);
            }
            const bool finalized = finalizeActiveWriteLocked(
                *activeWrite_,
                status,
                outcome.byteCount,
                category,
                outcome.nativeCode);
            if (!finalized) {
                state_ = svm::transport::SerialSessionState::Faulted;
                generation_ = svm::transport::kUnassignedSerialSessionGeneration;
                writeWorkerStopRequested_ = true;
                settlePendingWritesLocked(svm::transport::SerialErrorCategory::IoFailure);
            }
            activeCancellationCategory_ = svm::transport::SerialErrorCategory::None;
            if (writeWorkerStopRequested_) {
                return;
            }
        }
    }
}

DWORD WINAPI Win32SerialSession::writeWorkerThreadProc(void* parameter) {
    auto* self = static_cast<Win32SerialSession*>(parameter);
    if (self == nullptr) {
        return 1;
    }
    self->writeWorkerLoop();
    return 0;
}

svm::transport::SerialReadResult Win32SerialSession::readAvailable(
    std::size_t maxBytes,
    svm::transport::SerialDeadline deadline) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            return {
                .operation = rejectedClosed(
                    svm::transport::SerialOperationKind::Read,
                    snapshot,
                    deadline),
            };
        }
    }
    if (maxBytes == 0) {
        return {
            .operation = operationResult(
                svm::transport::SerialOperationKind::Read,
                svm::transport::SerialOperationStatus::RejectedInvalid,
                snapshot.generation,
                snapshot.endpoint,
                deadline,
                0,
                svm::transport::SerialErrorCategory::InvalidInput,
                0,
                svm::transport::kUnassignedSerialOperationId),
        };
    }
    const svm::transport::SerialOperationId operationId = allocateOperationId();
    if (deadlineExpired(deadline)) {
        return {
            .operation = operationResult(
                svm::transport::SerialOperationKind::Read,
                svm::transport::SerialOperationStatus::Timeout,
                snapshot.generation,
                snapshot.endpoint,
                deadline,
                0,
                svm::transport::SerialErrorCategory::Timeout,
                0,
                operationId),
        };
    }

    COMSTAT status = {};
    DWORD errors = 0;
    DWORD nativeCode = 0;
    std::vector<std::uint8_t> buffer;
    DWORD bytesRead = 0;
    bool leased = false;
    bool completedCurrent = false;
    {
        WriteLock ioLock(ioLock_);
        const HANDLE handle = validatedHandleForGeneration(snapshot.generation);
        leased = isValidHandle(handle);
        if (leased && !ClearCommError(handle, &errors, &status)) {
            nativeCode = GetLastError();
        } else if (leased && errors != 0) {
            nativeCode = errors;
        } else if (leased && status.cbInQue > 0) {
            const DWORD bytesToRead = static_cast<DWORD>(std::min<std::size_t>(
                maxBytes,
                std::min<std::size_t>(snapshot.options.readBufferSize, status.cbInQue)));
            buffer.resize(bytesToRead);
            if (!ReadFile(handle, buffer.data(), bytesToRead, &bytesRead, nullptr)) {
                nativeCode = GetLastError();
                buffer.clear();
            } else {
                buffer.resize(bytesRead);
            }
        }
        completedCurrent = leased
            && validatedHandleForGeneration(snapshot.generation) == handle;
    }
    if (!completedCurrent) {
        return {
            .operation = operationResult(
                svm::transport::SerialOperationKind::Read,
                svm::transport::SerialOperationStatus::Cancelled,
                snapshot.generation,
                snapshot.endpoint,
                deadline,
                0,
                svm::transport::SerialErrorCategory::SessionClosed,
                0,
                operationId),
        };
    }

    svm::transport::SerialOperationStatus operationStatus = svm::transport::SerialOperationStatus::Succeeded;
    svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None;
    if (nativeCode != 0) {
        category = errors != 0
            ? svm::transport::SerialErrorCategory::IoFailure
            : nativeErrorCategory(nativeCode, false);
        operationStatus = failureOperationStatus(category);
    } else if (deadlineExpired(deadline)) {
        operationStatus = svm::transport::SerialOperationStatus::Timeout;
        category = svm::transport::SerialErrorCategory::Timeout;
    }
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(snapshot.generation);
    }
    return {
        .operation = operationResult(
            svm::transport::SerialOperationKind::Read,
            operationStatus,
            snapshot.generation,
            snapshot.endpoint,
            deadline,
            buffer.size(),
            category,
            nativeCode,
            operationId),
        .bytes = std::move(buffer),
    };
}

svm::transport::SerialOperationResult Win32SerialSession::operationResult(
    svm::transport::SerialOperationKind kind,
    svm::transport::SerialOperationStatus status,
    svm::transport::SerialSessionGeneration generation,
    std::string operationEndpoint,
    svm::transport::SerialDeadline deadline,
    std::size_t byteCount,
    svm::transport::SerialErrorCategory category,
    std::uint32_t nativeCode,
    svm::transport::SerialOperationId requestId) {
    svm::transport::SerialDeadlineStatus deadlineStatus = svm::transport::SerialDeadlineStatus::NotSet;
    if (deadline.set()) {
        if (status == svm::transport::SerialOperationStatus::Accepted
            || status == svm::transport::SerialOperationStatus::RejectedInvalid
            || status == svm::transport::SerialOperationStatus::RejectedFull
            || status == svm::transport::SerialOperationStatus::RejectedClosed) {
            deadlineStatus = svm::transport::SerialDeadlineStatus::Pending;
        } else {
            deadlineStatus = status == svm::transport::SerialOperationStatus::Timeout
                ? svm::transport::SerialDeadlineStatus::Expired
                : svm::transport::SerialDeadlineStatus::Met;
        }
    }
    return {
        .operation = {
            .requestId = requestId,
            .generation = generation,
            .kind = kind,
            .deadline = deadline,
        },
        .status = status,
        .deadlineStatus = deadlineStatus,
        .byteCount = byteCount,
        .endpoint = std::move(operationEndpoint),
        .error = {
            .category = category,
            .nativeCode = nativeCode,
            .byteCount = byteCount,
        },
    };
}

svm::transport::SerialOperationResult Win32SerialSession::rejectedClosed(
    svm::transport::SerialOperationKind kind,
    const svm::transport::SerialSessionSnapshot& snapshot,
    svm::transport::SerialDeadline deadline) {
    return operationResult(
        kind,
        svm::transport::SerialOperationStatus::RejectedClosed,
        snapshot.generation,
        snapshot.endpoint,
        deadline,
        0,
        svm::transport::SerialErrorCategory::SessionClosed);
}

svm::transport::SerialTerminalResult Win32SerialSession::terminalResult(
    const svm::transport::SerialWriteResult& result,
    svm::transport::SerialOperationStatus status,
    svm::transport::SerialErrorCategory category,
    std::string operationEndpoint,
    std::uint32_t nativeCode) const {
    svm::transport::SerialDeadlineStatus deadlineStatus = svm::transport::SerialDeadlineStatus::NotSet;
    if (result.deadline.set()) {
        deadlineStatus = status == svm::transport::SerialOperationStatus::Timeout
            ? svm::transport::SerialDeadlineStatus::Expired
            : svm::transport::SerialDeadlineStatus::Met;
    }
    return {
        .operation = {
            .requestId = result.requestId,
            .generation = result.generation,
            .kind = svm::transport::SerialOperationKind::Write,
            .deadline = result.deadline,
        },
        .status = status,
        .deadlineStatus = deadlineStatus,
        .byteCount = result.byteCount,
        .endpoint = std::move(operationEndpoint),
        .error = {
            .category = category,
            .nativeCode = nativeCode,
            .byteCount = result.byteCount,
        },
    };
}

void Win32SerialSession::publishCompletion(svm::transport::SerialTerminalResult result) {
    completedWrites_.push_back(std::move(result));
}

bool Win32SerialSession::finalizeActiveWriteLocked(
    ActiveWrite request,
    svm::transport::SerialWriteResultStatus status,
    std::size_t byteCount,
    svm::transport::SerialErrorCategory category,
    std::uint32_t nativeCode) {
    if (!activeWrite_.has_value()
        || activeWrite_->requestId != request.requestId
        || activeWrite_->generation != request.generation) {
        return false;
    }
    const svm::transport::SerialWriteResult completed = writeQueue_.completeActive(
        request.requestId,
        request.generation,
        status,
        byteCount);
    if (completed.rejected()) {
        return false;
    }
    if (!activeWrite_->terminalPublished) {
        publishCompletion(
            terminalResult(
                completed,
                operationStatus(completed.status),
                category,
                request.endpoint,
                nativeCode));
    }
    activeWrite_.reset();
    return true;
}

void Win32SerialSession::publishInterruptedActiveWriteLocked(
    svm::transport::SerialErrorCategory category,
    std::uint32_t nativeCode) {
    if (!activeWrite_.has_value() || activeWrite_->terminalPublished) {
        return;
    }
    const svm::transport::SerialWriteResult interrupted{
        .requestId = activeWrite_->requestId,
        .generation = activeWrite_->generation,
        .status = category == svm::transport::SerialErrorCategory::Disconnected
            ? svm::transport::SerialWriteResultStatus::Disconnected
            : svm::transport::SerialWriteResultStatus::Closed,
        .byteCount = 0,
        .deadline = activeWrite_->deadline,
    };
    publishCompletion(
        terminalResult(
            interrupted,
            operationStatus(interrupted.status),
            category,
            activeWrite_->endpoint,
            nativeCode));
    activeWrite_->terminalPublished = true;
}

svm::transport::SerialErrorCategory Win32SerialSession::nativeErrorCategory(
    std::uint32_t nativeCode,
    bool cancellationRequested) noexcept {
    if (nativeCode == ERROR_SEM_TIMEOUT || nativeCode == ERROR_TIMEOUT) {
        return svm::transport::SerialErrorCategory::Timeout;
    }
    if (nativeCode == ERROR_OPERATION_ABORTED && cancellationRequested) {
        return svm::transport::SerialErrorCategory::Cancelled;
    }
    if (nativeCode == ERROR_DEVICE_NOT_CONNECTED
        || nativeCode == ERROR_INVALID_HANDLE
        || nativeCode == ERROR_GEN_FAILURE
        || nativeCode == ERROR_IO_DEVICE) {
        return svm::transport::SerialErrorCategory::Disconnected;
    }
    return nativeCode == 0
        ? svm::transport::SerialErrorCategory::IoFailure
        : svm::transport::SerialErrorCategory::NativeFailure;
}

svm::transport::SerialOperationStatus Win32SerialSession::operationStatus(
    svm::transport::SerialWriteResultStatus status) noexcept {
    switch (status) {
    case svm::transport::SerialWriteResultStatus::Sent:
        return svm::transport::SerialOperationStatus::Succeeded;
    case svm::transport::SerialWriteResultStatus::Timeout:
        return svm::transport::SerialOperationStatus::Timeout;
    case svm::transport::SerialWriteResultStatus::Cancelled:
    case svm::transport::SerialWriteResultStatus::Closed:
        return svm::transport::SerialOperationStatus::Cancelled;
    case svm::transport::SerialWriteResultStatus::Disconnected:
        return svm::transport::SerialOperationStatus::Disconnected;
    case svm::transport::SerialWriteResultStatus::RejectedFull:
        return svm::transport::SerialOperationStatus::RejectedFull;
    case svm::transport::SerialWriteResultStatus::RejectedInvalid:
        return svm::transport::SerialOperationStatus::RejectedInvalid;
    case svm::transport::SerialWriteResultStatus::Accepted:
        return svm::transport::SerialOperationStatus::Accepted;
    case svm::transport::SerialWriteResultStatus::Failed:
        return svm::transport::SerialOperationStatus::Failed;
    }
    return svm::transport::SerialOperationStatus::Failed;
}

bool Win32SerialSession::deadlineExpired(const svm::transport::SerialDeadline& deadline) noexcept {
    return deadline.expiresAt.has_value()
        && *deadline.expiresAt <= std::chrono::steady_clock::now();
}

svm::transport::SerialOperationId Win32SerialSession::allocateOperationId() noexcept {
    WriteLock lock(writeLock_);
    if (nextOperationId_ == 0) {
        return svm::transport::kUnassignedSerialOperationId;
    }
    const svm::transport::SerialOperationId operationId = nextOperationId_;
    if (nextOperationId_ == std::numeric_limits<svm::transport::SerialOperationId>::max()) {
        nextOperationId_ = 0;
    } else {
        ++nextOperationId_;
    }
    return operationId;
}

} // namespace svm::win32

#endif
