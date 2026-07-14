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
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace svm::win32 {
namespace {

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

bool setLastErrorText(std::string& lastErrorText, unsigned long errorCode, std::string_view operation) {
    lastErrorText = win32SerialErrorText(errorCode, operation);
    return false;
}

bool unsupportedDisabledControlLine(unsigned long errorCode, bool enabled) noexcept {
    return !enabled && errorCode == ERROR_NOT_SUPPORTED;
}

std::string commStatusErrorText(DWORD errors) {
    std::string message = "串口接收状态异常：";
    bool first = true;
    const auto appendFlag = [&](DWORD flag, std::string_view text) {
        if ((errors & flag) == 0) {
            return;
        }
        if (!first) {
            message.append("、");
        }
        message.append(text);
        first = false;
    };

    appendFlag(CE_BREAK, "检测到 break 信号");
    appendFlag(CE_FRAME, "帧错误");
    appendFlag(CE_OVERRUN, "驱动缓冲区溢出");
    appendFlag(CE_RXOVER, "接收缓冲区溢出");
    appendFlag(CE_RXPARITY, "校验错误");
    if (first) {
        message.append("未知线路错误");
    }
    message.append("。请检查波特率、校验位、停止位、接线和设备状态。");
    return message;
}

bool configureSerialState(
    HANDLE handle,
    const SerialOpenOptions& options,
    std::string& lastErrorText,
    DWORD& nativeCode) {
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle, &dcb)) {
        nativeCode = GetLastError();
        return setLastErrorText(lastErrorText, nativeCode, "读取串口参数");
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
        return setLastErrorText(lastErrorText, nativeCode, "设置串口参数");
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(options.readTimeoutMs);
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(options.writeTimeoutMs);
    if (!SetCommTimeouts(handle, &timeouts)) {
        nativeCode = GetLastError();
        return setLastErrorText(lastErrorText, nativeCode, "设置串口超时");
    }

    return true;
}

bool applyControlLines(
    HANDLE handle,
    const SerialOpenOptions& options,
    std::string& lastErrorText,
    DWORD& nativeCode) {
    if (!EscapeCommFunction(handle, options.dataTerminalReady ? SETDTR : CLRDTR)) {
        nativeCode = GetLastError();
        if (!unsupportedDisabledControlLine(nativeCode, options.dataTerminalReady)) {
            return setLastErrorText(lastErrorText, nativeCode, "设置 DTR 信号");
        }
        nativeCode = 0;
    }

    if (options.flowControl != SerialFlowControl::HardwareRtsCts
        && !EscapeCommFunction(handle, options.requestToSend ? SETRTS : CLRRTS)) {
        nativeCode = GetLastError();
        if (!unsupportedDisabledControlLine(nativeCode, options.requestToSend)) {
            return setLastErrorText(lastErrorText, nativeCode, "设置 RTS 信号");
        }
        nativeCode = 0;
    }

    return true;
}

} // namespace

Win32SerialSession::CapabilityView::CapabilityView(Win32SerialSession& owner) noexcept
    : owner_(owner) {
}

svm::transport::SerialOperationResult Win32SerialSession::CapabilityView::open(SerialOpenOptions options) {
    return owner_.openOperation(std::move(options));
}

svm::transport::SerialOperationResult Win32SerialSession::CapabilityView::close() {
    return owner_.closeOperation();
}

svm::transport::SerialSessionSnapshot Win32SerialSession::CapabilityView::snapshot() const {
    return owner_.sessionSnapshot();
}

svm::transport::SerialOperationResult Win32SerialSession::CapabilityView::setDataTerminalReady(bool enabled) {
    return owner_.setDataTerminalReadyOperation(enabled);
}

svm::transport::SerialOperationResult Win32SerialSession::CapabilityView::setRequestToSend(bool enabled) {
    return owner_.setRequestToSendOperation(enabled);
}

svm::transport::SerialByteStream& Win32SerialSession::CapabilityView::byteStream() noexcept {
    return *this;
}

svm::transport::SerialWriteScheduler& Win32SerialSession::CapabilityView::writeScheduler() noexcept {
    return *this;
}

svm::transport::SerialTerminalResult Win32SerialSession::CapabilityView::writeBytes(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    return owner_.writeOperation(std::move(payload), deadline);
}

svm::transport::SerialReadResult Win32SerialSession::CapabilityView::readAvailable(
    std::size_t maxBytes,
    svm::transport::SerialDeadline deadline) {
    return owner_.readOperation(maxBytes, deadline);
}

svm::transport::SerialWriteAdmissionResult Win32SerialSession::CapabilityView::enqueueWrite(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    return owner_.enqueueOperation(std::move(payload), deadline);
}

std::vector<svm::transport::SerialTerminalResult>
Win32SerialSession::CapabilityView::cancelPendingWrites() {
    return owner_.cancelPendingOperations();
}

std::vector<svm::transport::SerialTerminalResult>
Win32SerialSession::CapabilityView::takeCompletedWrites() {
    return owner_.takeCompletedOperations();
}

svm::transport::SerialWriteQueueSnapshot
Win32SerialSession::CapabilityView::writeQueueSnapshot() const {
    return owner_.writeQueueSnapshot();
}

Win32SerialSession::Win32SerialSession()
    : capabilityView_(*this) {
    InitializeCriticalSection(&lifecycleLock_);
    InitializeCriticalSection(&writeLock_);
    InitializeCriticalSection(&ioLock_);
}

Win32SerialSession::~Win32SerialSession() {
    close();
    if (writeWakeEvent_ != nullptr) {
        CloseHandle(writeWakeEvent_);
        writeWakeEvent_ = nullptr;
    }
    DeleteCriticalSection(&ioLock_);
    DeleteCriticalSection(&writeLock_);
    DeleteCriticalSection(&lifecycleLock_);
}

bool Win32SerialSession::open(SerialOpenOptions options) {
    return openOperation(std::move(options)).succeeded();
}

svm::transport::SerialOperationResult Win32SerialSession::openOperation(SerialOpenOptions options) {
    WriteLock lifecycleLock(lifecycleLock_);
    if (sessionSnapshot().state != svm::transport::SerialSessionState::Closed) {
        closeOperation();
    }

    const svm::transport::SerialOperationId operationId = allocateOperationId();
    const std::string requestedEndpoint = options.portName;
    {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Opening;
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
    }

    const SerialValidationResult validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Faulted;
        lastErrorText_ = validation.errorMessage;
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
    std::string diagnostic;
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
            diagnostic = win32SerialErrorText(nativeCode, "打开串口");
        } else if (!SetupComm(handle, static_cast<DWORD>(options.readBufferSize), 4096)) {
            nativeCode = GetLastError();
            diagnostic = win32SerialErrorText(nativeCode, "初始化串口缓冲区");
        } else if (!PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR)) {
            nativeCode = GetLastError();
            diagnostic = win32SerialErrorText(nativeCode, "清理串口缓冲区");
        } else if (!configureSerialState(handle, options, diagnostic, nativeCode)
            || !applyControlLines(handle, options, diagnostic, nativeCode)) {
        } else {
            handle_ = handle;
        }

        if (nativeCode != 0 && isValidHandle(handle)) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }

    if (nativeCode != 0 || !isValidHandle(handle)) {
        WriteLock lock(writeLock_);
        state_ = svm::transport::SerialSessionState::Faulted;
        lastErrorText_ = std::move(diagnostic);
        const svm::transport::SerialErrorCategory category = nativeErrorCategory(nativeCode, false);
        return operationResult(
            svm::transport::SerialOperationKind::Open,
            category == svm::transport::SerialErrorCategory::Disconnected
                ? svm::transport::SerialOperationStatus::Disconnected
                : svm::transport::SerialOperationStatus::Failed,
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
            generation_ = generationCounter_;
            options_ = std::move(options);
            options_.portName = stripWin32DevicePrefix(devicePath);
            state_ = svm::transport::SerialSessionState::Open;
            lastErrorText_.clear();
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
        state_ = svm::transport::SerialSessionState::Faulted;
        lastErrorText_ = "串口会话 generation 已耗尽。";
    }
    {
        WriteLock ioLock(ioLock_);
        CloseHandle(handle);
        handle_ = nullptr;
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
            svm::transport::SerialErrorCategory::IoFailure,
            0,
            operationId);
    }
}

void Win32SerialSession::close() {
    closeOperation();
}

svm::transport::SerialOperationResult Win32SerialSession::closeOperation() {
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

    stopWriteWorker();
    {
        WriteLock ioLock(ioLock_);
        HANDLE handle = asHandle(handle_);
        if (isValidHandle(handle)) {
            CloseHandle(handle);
        }
        handle_ = nullptr;
    }

    WriteLock lock(writeLock_);
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
        allocateOperationId());
}

bool Win32SerialSession::isOpen() const noexcept {
    WriteLock lock(writeLock_);
    return state_ == svm::transport::SerialSessionState::Open;
}

std::string Win32SerialSession::endpoint() const {
    WriteLock lock(writeLock_);
    return options_.portName;
}

std::string Win32SerialSession::lastErrorText() const {
    WriteLock lock(writeLock_);
    return lastErrorText_;
}

bool Win32SerialSession::usesHardwareRtsCts() const noexcept {
    WriteLock lock(writeLock_);
    return options_.flowControl == SerialFlowControl::HardwareRtsCts;
}

bool Win32SerialSession::setDataTerminalReady(bool enabled) {
    return setDataTerminalReadyOperation(enabled).succeeded();
}

bool Win32SerialSession::setRequestToSend(bool enabled) {
    return setRequestToSendOperation(enabled).succeeded();
}

svm::transport::SerialSession& Win32SerialSession::sessionCapability() noexcept {
    return capabilityView_;
}

svm::transport::SerialSessionSnapshot Win32SerialSession::sessionSnapshot() const {
    WriteLock lock(writeLock_);
    return sessionSnapshotLocked();
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
Win32SerialSession::setDataTerminalReadyOperation(bool enabled) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            lastErrorText_ = "串口未打开，无法设置 DTR 信号。";
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
    const std::string diagnostic = nativeCode == 0
        ? std::string{}
        : win32SerialErrorText(nativeCode, "设置 DTR 信号");
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(snapshot.generation, diagnostic);
    }
    {
        WriteLock lock(writeLock_);
        if (state_ == svm::transport::SerialSessionState::Open && generation_ == snapshot.generation) {
            lastErrorText_ = diagnostic;
            if (nativeCode == 0) {
                options_.dataTerminalReady = enabled;
            }
        }
    }
    if (nativeCode != 0) {
        return operationResult(
            svm::transport::SerialOperationKind::SetDataTerminalReady,
            category == svm::transport::SerialErrorCategory::Disconnected
                ? svm::transport::SerialOperationStatus::Disconnected
                : svm::transport::SerialOperationStatus::Failed,
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
Win32SerialSession::setRequestToSendOperation(bool enabled) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            lastErrorText_ = "串口未打开，无法设置 RTS 信号。";
            return rejectedClosed(svm::transport::SerialOperationKind::SetRequestToSend, snapshot);
        }
        if (snapshot.usesHardwareRtsCts()) {
            lastErrorText_ = "RTS 正由 RTS/CTS 硬件流控自动管理，不能手动切换。";
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
    const std::string diagnostic = nativeCode == 0
        ? std::string{}
        : win32SerialErrorText(nativeCode, "设置 RTS 信号");
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(snapshot.generation, diagnostic);
    }
    {
        WriteLock lock(writeLock_);
        if (state_ == svm::transport::SerialSessionState::Open && generation_ == snapshot.generation) {
            lastErrorText_ = diagnostic;
            if (nativeCode == 0) {
                options_.requestToSend = enabled;
            }
        }
    }
    if (nativeCode != 0) {
        return operationResult(
            svm::transport::SerialOperationKind::SetRequestToSend,
            category == svm::transport::SerialErrorCategory::Disconnected
                ? svm::transport::SerialOperationStatus::Disconnected
                : svm::transport::SerialOperationStatus::Failed,
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

SerialIoResult Win32SerialSession::writeBytes(const std::vector<std::uint8_t>& payload) {
    return writeBytes(payload.data(), payload.size());
}

SerialIoResult Win32SerialSession::writeBytes(const std::uint8_t* payload, std::size_t size) {
    NativeIoOutcome outcome = writeBytesInternal(payload, size, true);
    return {
        .ok = outcome.ok,
        .byteCount = outcome.byteCount,
        .errorMessage = std::move(outcome.diagnostic),
    };
}

Win32SerialSession::NativeIoOutcome Win32SerialSession::writeBytesInternal(
    const std::uint8_t* payload,
    std::size_t size,
    bool updateLastError,
    svm::transport::SerialSessionGeneration expectedGeneration) {
    svm::transport::SerialSessionGeneration generation = expectedGeneration;
    {
        WriteLock lock(writeLock_);
        if (state_ != svm::transport::SerialSessionState::Open
            || (generation != svm::transport::kUnassignedSerialSessionGeneration
                && generation_ != generation)) {
            NativeIoOutcome outcome{
                .ok = false,
                .category = svm::transport::SerialErrorCategory::SessionClosed,
                .diagnostic = "串口未打开，无法发送数据。",
            };
            if (updateLastError
                && (generation == svm::transport::kUnassignedSerialSessionGeneration
                    || generation_ == generation)) {
                lastErrorText_ = outcome.diagnostic;
            }
            return outcome;
        }
        generation = generation_;
    }
    if (size > 0 && payload == nullptr) {
        NativeIoOutcome outcome{
            .ok = false,
            .category = svm::transport::SerialErrorCategory::InvalidInput,
            .diagnostic = "待发送数据为空指针，无法写入串口。",
        };
        if (updateLastError) {
            WriteLock lock(writeLock_);
            if (state_ == svm::transport::SerialSessionState::Open && generation_ == generation) {
                lastErrorText_ = outcome.diagnostic;
            }
        }
        return outcome;
    }

    NativeIoOutcome outcome;
    {
        WriteLock ioLock(ioLock_);
        const HANDLE handle = validatedHandleForGeneration(generation);
        outcome = isValidHandle(handle)
            ? writeBytesToHandle(handle, payload, size)
            : NativeIoOutcome{
                  .ok = false,
                  .category = svm::transport::SerialErrorCategory::SessionClosed,
                  .diagnostic = "串口会话已在写入前关闭。",
              };
        if (isValidHandle(handle) && validatedHandleForGeneration(generation) != handle) {
            outcome.ok = false;
            outcome.category = svm::transport::SerialErrorCategory::SessionClosed;
            outcome.diagnostic = "串口会话已在写入期间关闭。";
        }
    }
    if (outcome.category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(generation, outcome.diagnostic);
    }
    if (updateLastError) {
        WriteLock lock(writeLock_);
        if (state_ == svm::transport::SerialSessionState::Open && generation_ == generation) {
            lastErrorText_ = outcome.ok ? std::string{} : outcome.diagnostic;
        }
    }
    return outcome;
}

Win32SerialSession::NativeIoOutcome Win32SerialSession::writeBytesToHandle(
    HANDLE handle,
    const std::uint8_t* payload,
    std::size_t size) {
    if (size == 0) {
        return {.ok = true};
    }

    std::size_t totalWritten = 0;
    while (totalWritten < size) {
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
                .diagnostic = win32SerialErrorText(nativeCode, "写入串口"),
            };
        }

        totalWritten += written;
        if (written == 0) {
            return {
                .ok = false,
                .byteCount = totalWritten,
                .category = svm::transport::SerialErrorCategory::IoFailure,
                .diagnostic = "串口驱动没有写入任何字节。设备可能已断开、缓冲区已满，或流控阻止继续发送。",
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

svm::transport::SerialTerminalResult Win32SerialSession::writeOperation(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    const auto snapshot = sessionSnapshot();
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
    const svm::transport::SerialOperationId requestId = allocateOperationId();
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
            requestId);
    }

    NativeIoOutcome outcome = writeBytesInternal(
        payload.data(),
        payload.size(),
        true,
        snapshot.generation);
    svm::transport::SerialOperationStatus status = svm::transport::SerialOperationStatus::Succeeded;
    svm::transport::SerialErrorCategory category = outcome.category;
    if (deadlineExpired(deadline)) {
        status = svm::transport::SerialOperationStatus::Timeout;
        category = svm::transport::SerialErrorCategory::Timeout;
    } else if (!outcome.ok) {
        status = outcome.category == svm::transport::SerialErrorCategory::SessionClosed
            ? svm::transport::SerialOperationStatus::Cancelled
            : (outcome.category == svm::transport::SerialErrorCategory::Disconnected
                  ? svm::transport::SerialOperationStatus::Disconnected
                  : (outcome.category == svm::transport::SerialErrorCategory::Timeout
                        ? svm::transport::SerialOperationStatus::Timeout
                        : svm::transport::SerialOperationStatus::Failed));
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
        requestId);
}

svm::transport::SerialWriteResult Win32SerialSession::enqueueWrite(
    std::vector<std::uint8_t> payload,
    std::optional<int> timeoutMs) {
    return enqueueWriteCore(std::move(payload), timeoutMs, {}).result;
}

Win32SerialSession::WriteAdmission Win32SerialSession::enqueueWriteCore(
    std::vector<std::uint8_t> payload,
    std::optional<int> timeoutMs,
    svm::transport::SerialDeadline deadline) {
    WriteAdmission admission;
    bool wakeWorker = false;
    {
        WriteLock lock(writeLock_);
        admission.endpoint = options_.portName;
        if (state_ != svm::transport::SerialSessionState::Open) {
            lastErrorText_ = "串口未打开，无法发送数据。";
            admission.result = {
                .generation = generation_,
                .status = svm::transport::SerialWriteResultStatus::Failed,
                .deadline = deadline,
                .message = lastErrorText_,
            };
            admission.status = svm::transport::SerialOperationStatus::RejectedClosed;
            admission.category = svm::transport::SerialErrorCategory::SessionClosed;
            return admission;
        }

        admission.result = writeQueue_.enqueue(
            std::move(payload),
            timeoutMs.value_or(std::max(1, options_.writeTimeoutMs)),
            generation_,
            deadline);
        if (admission.result.accepted() && !ensureWriteWorkerLocked()) {
            writeQueue_.cancelPending(admission.result.requestId);
            admission.result = {
                .requestId = admission.result.requestId,
                .generation = admission.result.generation,
                .status = svm::transport::SerialWriteResultStatus::Failed,
                .byteCount = 0,
                .deadline = admission.result.deadline,
                .message = lastErrorText_.empty() ? "启动串口写入后台线程失败。" : lastErrorText_,
            };
            admission.status = svm::transport::SerialOperationStatus::Failed;
            admission.category = svm::transport::SerialErrorCategory::NativeFailure;
        } else if (admission.result.accepted()) {
            admission.status = svm::transport::SerialOperationStatus::Accepted;
            admission.category = svm::transport::SerialErrorCategory::None;
            wakeWorker = true;
        } else if (admission.result.status == svm::transport::SerialWriteResultStatus::RejectedFull) {
            admission.status = svm::transport::SerialOperationStatus::RejectedFull;
            admission.category = svm::transport::SerialErrorCategory::QueueFull;
        } else {
            admission.status = svm::transport::SerialOperationStatus::RejectedInvalid;
            admission.category = svm::transport::SerialErrorCategory::InvalidInput;
        }
        if (admission.result.accepted()) {
            lastErrorText_.clear();
        } else if (!admission.result.message.empty()) {
            lastErrorText_ = admission.result.message;
        }
    }

    if (wakeWorker && writeWakeEvent_ != nullptr) {
        SetEvent(writeWakeEvent_);
    }
    return admission;
}

svm::transport::SerialWriteAdmissionResult Win32SerialSession::enqueueOperation(
    std::vector<std::uint8_t> payload,
    svm::transport::SerialDeadline deadline) {
    const WriteAdmission admission = enqueueWriteCore(
        std::move(payload),
        std::nullopt,
        deadline);
    return operationResult(
        svm::transport::SerialOperationKind::Write,
        admission.status,
        admission.result.generation,
        admission.endpoint,
        admission.result.deadline,
        admission.result.byteCount,
        admission.category,
        0,
        admission.result.requestId);
}

std::vector<svm::transport::SerialWriteResult> Win32SerialSession::cancelPendingWrites() {
    const auto typedResults = cancelPendingOperations();
    std::vector<svm::transport::SerialWriteResult> results;
    results.reserve(typedResults.size());
    for (const auto& result : typedResults) {
        results.push_back({
            .requestId = result.operation.requestId,
            .generation = result.operation.generation,
            .status = svm::transport::SerialWriteResultStatus::Cancelled,
            .byteCount = result.byteCount,
            .deadline = result.operation.deadline,
        });
    }
    return results;
}

std::vector<svm::transport::SerialTerminalResult>
Win32SerialSession::cancelPendingOperations() {
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
    }
    if (writeWakeEvent_ != nullptr) {
        SetEvent(writeWakeEvent_);
    }
    return results;
}

std::vector<svm::transport::SerialWriteResult> Win32SerialSession::takeCompletedWrites() {
    WriteLock lock(writeLock_);
    std::vector<svm::transport::SerialWriteResult> results;
    results.reserve(completedWrites_.size());
    while (!completedWrites_.empty()) {
        results.push_back(legacyResult(completedWrites_.front()));
        completedWrites_.pop_front();
    }
    return results;
}

std::vector<svm::transport::SerialTerminalResult>
Win32SerialSession::takeCompletedOperations() {
    WriteLock lock(writeLock_);
    std::vector<svm::transport::SerialTerminalResult> results;
    results.reserve(completedWrites_.size());
    while (!completedWrites_.empty()) {
        results.push_back(std::move(completedWrites_.front().result));
        completedWrites_.pop_front();
    }
    return results;
}

svm::transport::SerialWriteQueueSnapshot Win32SerialSession::writeQueueSnapshot() const {
    WriteLock lock(writeLock_);
    return writeQueue_.snapshot();
}

bool Win32SerialSession::ensureWriteWorkerLocked() {
    if (writeThread_ != nullptr) {
        return true;
    }
    if (writeWakeEvent_ == nullptr) {
        writeWakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (writeWakeEvent_ == nullptr) {
            lastErrorText_ = win32SerialErrorText(GetLastError(), "创建串口写入事件");
            return false;
        }
    }
    writeWorkerStopRequested_ = false;
    writeThread_ = CreateThread(nullptr, 0, &Win32SerialSession::writeWorkerThreadProc, this, 0, nullptr);
    if (writeThread_ == nullptr) {
        lastErrorText_ = win32SerialErrorText(GetLastError(), "启动串口写入线程");
        return false;
    }
    return true;
}

void Win32SerialSession::settlePendingWritesLocked(
    svm::transport::SerialErrorCategory category) {
    const bool disconnected = category == svm::transport::SerialErrorCategory::Disconnected;
    const auto status = disconnected
        ? svm::transport::SerialOperationStatus::Disconnected
        : svm::transport::SerialOperationStatus::Cancelled;
    const std::string currentEndpoint = options_.portName;
    for (const auto& cancelled : writeQueue_.cancelAllPending()) {
        publishCompletion(terminalResult(cancelled, status, category, currentEndpoint));
    }
}

void Win32SerialSession::markGenerationDisconnected(
    svm::transport::SerialSessionGeneration generation,
    const std::string& diagnostic) {
    bool wakeWorker = false;
    {
        WriteLock lock(writeLock_);
        if (state_ != svm::transport::SerialSessionState::Open || generation_ != generation) {
            return;
        }
        state_ = svm::transport::SerialSessionState::Faulted;
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
        lastErrorText_ = diagnostic;
        writeWorkerStopRequested_ = true;
        if (activeWrite_.has_value()) {
            activeCancellationCategory_ = svm::transport::SerialErrorCategory::Disconnected;
        }
        settlePendingWritesLocked(svm::transport::SerialErrorCategory::Disconnected);
        wakeWorker = true;
    }
    if (wakeWorker && writeWakeEvent_ != nullptr) {
        SetEvent(writeWakeEvent_);
    }
}

void Win32SerialSession::stopWriteWorker(svm::transport::SerialErrorCategory category) {
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
    if (writeWakeEvent_ != nullptr) {
        SetEvent(writeWakeEvent_);
    }
    if (thread != nullptr) {
        CancelSynchronousIo(thread);
        if (isValidHandle(handle)) {
            PurgeComm(handle, PURGE_TXABORT | PURGE_RXABORT);
        }
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    {
        WriteLock lock(writeLock_);
        if (activeWrite_.has_value()) {
            const bool disconnected = activeCategory == svm::transport::SerialErrorCategory::Disconnected;
            completeActiveWrite(
                *activeWrite_,
                disconnected
                    ? svm::transport::SerialWriteResultStatus::Disconnected
                    : svm::transport::SerialWriteResultStatus::Closed,
                0,
                activeCategory,
                0,
                {});
        }
        if (writeThread_ == thread) {
            writeThread_ = nullptr;
        }
        writeWorkerStopRequested_ = false;
        activeCancellationCategory_ = svm::transport::SerialErrorCategory::None;
        activeWrite_.reset();
    }
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
            WaitForSingleObject(writeWakeEvent_, INFINITE);
        }

        if (!request.has_value()) {
            continue;
        }

        NativeIoOutcome outcome;
        if (deadlineExpired(request->deadline)) {
            outcome = {
                .ok = false,
                .category = svm::transport::SerialErrorCategory::Timeout,
                .diagnostic = "串口写入请求在执行前已超时。",
            };
        } else {
            outcome = writeBytesInternal(
                request->payload.data(),
                request->payload.size(),
                false,
                request->generation);
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
            } else if (deadlineExpired(request->deadline)
                || outcome.category == svm::transport::SerialErrorCategory::Timeout) {
                status = svm::transport::SerialWriteResultStatus::Timeout;
                category = svm::transport::SerialErrorCategory::Timeout;
            } else if (!outcome.ok) {
                status = outcome.category == svm::transport::SerialErrorCategory::Disconnected
                    ? svm::transport::SerialWriteResultStatus::Disconnected
                    : (outcome.category == svm::transport::SerialErrorCategory::Cancelled
                          ? svm::transport::SerialWriteResultStatus::Cancelled
                          : svm::transport::SerialWriteResultStatus::Failed);
            }
            if (category == svm::transport::SerialErrorCategory::Disconnected
                && state_ == svm::transport::SerialSessionState::Open
                && generation_ == request->generation) {
                state_ = svm::transport::SerialSessionState::Faulted;
                generation_ = svm::transport::kUnassignedSerialSessionGeneration;
                lastErrorText_ = outcome.diagnostic;
                writeWorkerStopRequested_ = true;
                settlePendingWritesLocked(svm::transport::SerialErrorCategory::Disconnected);
            }
            completeActiveWrite(
                *activeWrite_,
                status,
                outcome.byteCount,
                category,
                outcome.nativeCode,
                std::move(outcome.diagnostic));
            activeWrite_.reset();
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

bool Win32SerialSession::waitForReadyRead(int timeoutMs) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            lastErrorText_ = "串口未打开，无法等待接收数据。";
            return false;
        }
    }

    const ULONGLONG startedAt = GetTickCount64();
    const ULONGLONG safeTimeout = timeoutMs > 0 ? static_cast<ULONGLONG>(timeoutMs) : 0;

    while (true) {
        COMSTAT status = {};
        DWORD errors = 0;
        DWORD nativeCode = 0;
        bool leased = false;
        bool completedCurrent = false;
        {
            WriteLock ioLock(ioLock_);
            const HANDLE handle = validatedHandleForGeneration(snapshot.generation);
            leased = isValidHandle(handle);
            if (leased && !ClearCommError(handle, &errors, &status)) {
                nativeCode = GetLastError();
            }
            completedCurrent = leased
                && validatedHandleForGeneration(snapshot.generation) == handle;
        }
        if (!completedCurrent) {
            return false;
        }
        if (nativeCode != 0 || errors != 0) {
            const std::string diagnostic = errors != 0
                ? commStatusErrorText(errors)
                : win32SerialErrorText(nativeCode, "查询串口接收状态");
            if (nativeCode != 0
                && nativeErrorCategory(nativeCode, false)
                    == svm::transport::SerialErrorCategory::Disconnected) {
                markGenerationDisconnected(snapshot.generation, diagnostic);
            }
            WriteLock lock(writeLock_);
            if (state_ == svm::transport::SerialSessionState::Open
                && generation_ == snapshot.generation) {
                lastErrorText_ = diagnostic;
            }
            return false;
        }

        if (status.cbInQue > 0) {
            WriteLock lock(writeLock_);
            if (state_ == svm::transport::SerialSessionState::Open
                && generation_ == snapshot.generation) {
                lastErrorText_.clear();
            }
            return true;
        }

        if (safeTimeout == 0 || GetTickCount64() - startedAt >= safeTimeout) {
            return false;
        }

        const ULONGLONG elapsed = GetTickCount64() - startedAt;
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(safeTimeout - elapsed, 10));
        Sleep(std::max<DWORD>(remaining, 1));
    }
}

std::vector<std::uint8_t> Win32SerialSession::readAvailable(std::size_t maxBytes) {
    return readOperation(maxBytes, {}).bytes;
}

svm::transport::SerialReadResult Win32SerialSession::readOperation(
    std::size_t maxBytes,
    svm::transport::SerialDeadline deadline) {
    svm::transport::SerialSessionSnapshot snapshot;
    {
        WriteLock lock(writeLock_);
        snapshot = sessionSnapshotLocked();
        if (!snapshot.open()) {
            lastErrorText_ = "串口未打开，无法读取数据。";
            return {
                .operation = rejectedClosed(
                    svm::transport::SerialOperationKind::Read,
                    snapshot,
                    deadline),
            };
        }
    }
    if (maxBytes == 0) {
        WriteLock lock(writeLock_);
        if (state_ == svm::transport::SerialSessionState::Open
            && generation_ == snapshot.generation) {
            lastErrorText_.clear();
        }
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
    std::string diagnostic;
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
            diagnostic = win32SerialErrorText(nativeCode, "查询串口接收状态");
        } else if (leased && errors != 0) {
            nativeCode = errors;
            diagnostic = commStatusErrorText(errors);
        } else if (leased && status.cbInQue > 0) {
            const DWORD bytesToRead = static_cast<DWORD>(std::min<std::size_t>(
                maxBytes,
                std::min<std::size_t>(snapshot.options.readBufferSize, status.cbInQue)));
            buffer.resize(bytesToRead);
            if (!ReadFile(handle, buffer.data(), bytesToRead, &bytesRead, nullptr)) {
                nativeCode = GetLastError();
                diagnostic = win32SerialErrorText(nativeCode, "读取串口");
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
    if (deadlineExpired(deadline)) {
        operationStatus = svm::transport::SerialOperationStatus::Timeout;
        category = svm::transport::SerialErrorCategory::Timeout;
    } else if (nativeCode != 0) {
        category = errors != 0
            ? svm::transport::SerialErrorCategory::IoFailure
            : nativeErrorCategory(nativeCode, false);
        operationStatus = category == svm::transport::SerialErrorCategory::Disconnected
            ? svm::transport::SerialOperationStatus::Disconnected
            : (category == svm::transport::SerialErrorCategory::Timeout
                  ? svm::transport::SerialOperationStatus::Timeout
                  : svm::transport::SerialOperationStatus::Failed);
    }
    if (category == svm::transport::SerialErrorCategory::Disconnected) {
        markGenerationDisconnected(snapshot.generation, diagnostic);
    }
    {
        WriteLock lock(writeLock_);
        if (state_ == svm::transport::SerialSessionState::Open
            && generation_ == snapshot.generation) {
            lastErrorText_ = operationStatus == svm::transport::SerialOperationStatus::Succeeded
                ? std::string{}
                : diagnostic;
        }
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

void Win32SerialSession::publishCompletion(
    svm::transport::SerialTerminalResult result,
    std::string diagnostic) {
    completedWrites_.push_back({
        .result = std::move(result),
        .diagnostic = std::move(diagnostic),
    });
}

bool Win32SerialSession::completeActiveWrite(
    const ActiveWrite& request,
    svm::transport::SerialWriteResultStatus status,
    std::size_t byteCount,
    svm::transport::SerialErrorCategory category,
    std::uint32_t nativeCode,
    std::string diagnostic) {
    const svm::transport::SerialWriteResult completed = writeQueue_.completeActive(
        request.requestId,
        request.generation,
        status,
        byteCount,
        diagnostic);
    if (completed.rejected()) {
        return false;
    }
    publishCompletion(
        terminalResult(
            completed,
            operationStatus(completed.status),
            category,
            request.endpoint,
            nativeCode),
        std::move(diagnostic));
    return true;
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
        || nativeCode == ERROR_GEN_FAILURE) {
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

svm::transport::SerialWriteResult Win32SerialSession::legacyResult(const CompletedWrite& completed) {
    svm::transport::SerialWriteResultStatus status = svm::transport::SerialWriteResultStatus::Failed;
    switch (completed.result.status) {
    case svm::transport::SerialOperationStatus::Succeeded:
        status = svm::transport::SerialWriteResultStatus::Sent;
        break;
    case svm::transport::SerialOperationStatus::Timeout:
        status = svm::transport::SerialWriteResultStatus::Timeout;
        break;
    case svm::transport::SerialOperationStatus::Cancelled:
        status = svm::transport::SerialWriteResultStatus::Cancelled;
        break;
    case svm::transport::SerialOperationStatus::Disconnected:
        status = svm::transport::SerialWriteResultStatus::Disconnected;
        break;
    case svm::transport::SerialOperationStatus::Accepted:
        status = svm::transport::SerialWriteResultStatus::Accepted;
        break;
    case svm::transport::SerialOperationStatus::RejectedFull:
        status = svm::transport::SerialWriteResultStatus::RejectedFull;
        break;
    case svm::transport::SerialOperationStatus::RejectedInvalid:
    case svm::transport::SerialOperationStatus::RejectedClosed:
        status = svm::transport::SerialWriteResultStatus::RejectedInvalid;
        break;
    case svm::transport::SerialOperationStatus::Failed:
        status = svm::transport::SerialWriteResultStatus::Failed;
        break;
    }
    return {
        .requestId = completed.result.operation.requestId,
        .generation = completed.result.operation.generation,
        .status = status,
        .byteCount = completed.result.byteCount,
        .deadline = completed.result.operation.deadline,
        .message = completed.diagnostic,
    };
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
