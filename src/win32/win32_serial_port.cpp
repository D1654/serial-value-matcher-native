#include "win32/win32_serial_port.h"

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

bool isTimeoutErrorText(std::string_view message) noexcept {
    return message.find("超时") != std::string_view::npos
        || message.find("121") != std::string_view::npos;
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

bool configureSerialState(HANDLE handle, const SerialOpenOptions& options, std::string& lastErrorText) {
    DCB dcb = {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(handle, &dcb)) {
        return setLastErrorText(lastErrorText, GetLastError(), "读取串口参数");
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
        return setLastErrorText(lastErrorText, GetLastError(), "设置串口参数");
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(options.readTimeoutMs);
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(options.writeTimeoutMs);
    if (!SetCommTimeouts(handle, &timeouts)) {
        return setLastErrorText(lastErrorText, GetLastError(), "设置串口超时");
    }

    return true;
}

bool applyControlLines(HANDLE handle, const SerialOpenOptions& options, std::string& lastErrorText) {
    if (!EscapeCommFunction(handle, options.dataTerminalReady ? SETDTR : CLRDTR)) {
        const unsigned long errorCode = GetLastError();
        if (!unsupportedDisabledControlLine(errorCode, options.dataTerminalReady)) {
            return setLastErrorText(lastErrorText, errorCode, "设置 DTR 信号");
        }
    }

    if (options.flowControl != SerialFlowControl::HardwareRtsCts
        && !EscapeCommFunction(handle, options.requestToSend ? SETRTS : CLRRTS)) {
        const unsigned long errorCode = GetLastError();
        if (!unsupportedDisabledControlLine(errorCode, options.requestToSend)) {
            return setLastErrorText(lastErrorText, errorCode, "设置 RTS 信号");
        }
    }

    return true;
}

} // namespace

Win32SerialPort::Win32SerialPort() {
    InitializeCriticalSection(&writeLock_);
}

Win32SerialPort::~Win32SerialPort() {
    close();
    if (writeWakeEvent_ != nullptr) {
        CloseHandle(writeWakeEvent_);
        writeWakeEvent_ = nullptr;
    }
    DeleteCriticalSection(&writeLock_);
}

bool Win32SerialPort::open(SerialOpenOptions options) {
    close();

    const SerialValidationResult validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        lastErrorText_ = validation.errorMessage;
        return false;
    }

    const std::string devicePath = makeWin32DevicePath(options.portName);
    const std::wstring wideDevicePath = utf8ToWide(devicePath);
    HANDLE handle = CreateFileW(
        wideDevicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (!isValidHandle(handle)) {
        return setLastErrorText(lastErrorText_, GetLastError(), "打开串口");
    }

    if (!SetupComm(handle, static_cast<DWORD>(options.readBufferSize), 4096)) {
        const unsigned long errorCode = GetLastError();
        CloseHandle(handle);
        return setLastErrorText(lastErrorText_, errorCode, "初始化串口缓冲区");
    }
    if (!PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR)) {
        const unsigned long errorCode = GetLastError();
        CloseHandle(handle);
        return setLastErrorText(lastErrorText_, errorCode, "清理串口缓冲区");
    }

    if (!configureSerialState(handle, options, lastErrorText_) || !applyControlLines(handle, options, lastErrorText_)) {
        CloseHandle(handle);
        return false;
    }

    handle_ = handle;
    options_ = std::move(options);
    options_.portName = stripWin32DevicePrefix(devicePath);
    lastErrorText_.clear();
    return true;
}

void Win32SerialPort::close() {
    stopWriteWorker();

    HANDLE handle = asHandle(handle_);
    if (!isValidHandle(handle)) {
        handle_ = nullptr;
        writeQueue_.clear();
        return;
    }

    CloseHandle(handle);
    handle_ = nullptr;
    writeQueue_.clear();
}

bool Win32SerialPort::isOpen() const noexcept {
    return isValidHandle(asHandle(handle_));
}

std::string Win32SerialPort::endpoint() const {
    return options_.portName;
}

std::string Win32SerialPort::lastErrorText() const {
    return lastErrorText_;
}

bool Win32SerialPort::usesHardwareRtsCts() const noexcept {
    return options_.flowControl == SerialFlowControl::HardwareRtsCts;
}

bool Win32SerialPort::setDataTerminalReady(bool enabled) {
    if (!isOpen()) {
        lastErrorText_ = "串口未打开，无法设置 DTR 信号。";
        return false;
    }

    HANDLE handle = asHandle(handle_);
    if (!EscapeCommFunction(handle, enabled ? SETDTR : CLRDTR)) {
        return setLastErrorText(lastErrorText_, GetLastError(), "设置 DTR 信号");
    }
    options_.dataTerminalReady = enabled;
    lastErrorText_.clear();
    return true;
}

bool Win32SerialPort::setRequestToSend(bool enabled) {
    if (!isOpen()) {
        lastErrorText_ = "串口未打开，无法设置 RTS 信号。";
        return false;
    }
    if (usesHardwareRtsCts()) {
        lastErrorText_ = "RTS 正由 RTS/CTS 硬件流控自动管理，不能手动切换。";
        return false;
    }

    HANDLE handle = asHandle(handle_);
    if (!EscapeCommFunction(handle, enabled ? SETRTS : CLRRTS)) {
        return setLastErrorText(lastErrorText_, GetLastError(), "设置 RTS 信号");
    }
    options_.requestToSend = enabled;
    lastErrorText_.clear();
    return true;
}

SerialIoResult Win32SerialPort::writeBytes(const std::vector<std::uint8_t>& payload) {
    return writeBytes(payload.data(), payload.size());
}

SerialIoResult Win32SerialPort::writeBytes(const std::uint8_t* payload, std::size_t size) {
    return writeBytesInternal(payload, size, true);
}

SerialIoResult Win32SerialPort::writeBytesInternal(const std::uint8_t* payload, std::size_t size, bool updateLastError) {
    const auto fail = [this, updateLastError](std::size_t byteCount, std::string message) {
        if (updateLastError) {
            lastErrorText_ = message;
        }
        return SerialIoResult{false, byteCount, std::move(message)};
    };
    const auto succeed = [this, updateLastError](std::size_t byteCount) {
        if (updateLastError) {
            lastErrorText_.clear();
        }
        return SerialIoResult{true, byteCount, {}};
    };

    if (!isOpen()) {
        return fail(0, "串口未打开，无法发送数据。");
    }

    if (size == 0) {
        return succeed(0);
    }

    if (payload == nullptr) {
        return fail(0, "待发送数据为空指针，无法写入串口。");
    }

    HANDLE handle = asHandle(handle_);
    std::size_t totalWritten = 0;
    while (totalWritten < size) {
        const std::size_t remaining = size - totalWritten;
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, payload + totalWritten, chunkSize, &written, nullptr)) {
            return fail(totalWritten, win32SerialErrorText(GetLastError(), "写入串口"));
        }

        totalWritten += written;
        if (written == 0) {
            return fail(totalWritten, "串口驱动没有写入任何字节。设备可能已断开、缓冲区已满，或流控阻止继续发送。");
        }
    }

    return succeed(totalWritten);
}

svm::transport::SerialWriteResult Win32SerialPort::enqueueWrite(std::vector<std::uint8_t> payload) {
    if (!isOpen()) {
        lastErrorText_ = "串口未打开，无法发送数据。";
        return {
            .status = svm::transport::SerialWriteResultStatus::Failed,
            .message = lastErrorText_,
        };
    }

    svm::transport::SerialWriteResult result;
    {
        WriteLock lock(writeLock_);
        const auto snapshot = writeQueue_.snapshot();
        if (writeInProgress_ && snapshot.capacity > 0 && snapshot.pendingCount + 1 >= snapshot.capacity) {
            result = {
                .status = svm::transport::SerialWriteResultStatus::RejectedFull,
                .message = "串口写入队列已满，请等待前序请求完成。",
            };
        } else {
            result = writeQueue_.enqueue(std::move(payload), std::max(1, options_.writeTimeoutMs));
        }
        if (result.accepted() && !ensureWriteWorkerLocked()) {
            writeQueue_.cancelPending(result.requestId);
            result = {
                .requestId = result.requestId,
                .status = svm::transport::SerialWriteResultStatus::Failed,
                .message = lastErrorText_.empty() ? "启动串口写入后台线程失败。" : lastErrorText_,
            };
        }
    }

    if (result.accepted()) {
        lastErrorText_.clear();
        SetEvent(writeWakeEvent_);
    } else if (!result.message.empty()) {
        lastErrorText_ = result.message;
    }
    return result;
}

std::vector<svm::transport::SerialWriteResult> Win32SerialPort::cancelPendingWrites() {
    std::vector<svm::transport::SerialWriteResult> results;
    {
        WriteLock lock(writeLock_);
        results = writeQueue_.cancelAllPending();
        for (const auto& result : results) {
            completedWrites_.push_back(result);
        }
    }
    if (writeWakeEvent_ != nullptr) {
        SetEvent(writeWakeEvent_);
    }
    return results;
}

std::vector<svm::transport::SerialWriteResult> Win32SerialPort::takeCompletedWrites() {
    WriteLock lock(writeLock_);
    std::vector<svm::transport::SerialWriteResult> results;
    results.reserve(completedWrites_.size());
    while (!completedWrites_.empty()) {
        results.push_back(std::move(completedWrites_.front()));
        completedWrites_.pop_front();
    }
    return results;
}

svm::transport::SerialWriteQueueSnapshot Win32SerialPort::writeQueueSnapshot() const {
    WriteLock lock(writeLock_);
    auto snapshot = writeQueue_.snapshot();
    if (writeInProgress_) {
        ++snapshot.pendingCount;
    }
    return snapshot;
}

bool Win32SerialPort::ensureWriteWorkerLocked() {
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
    writeThread_ = CreateThread(nullptr, 0, &Win32SerialPort::writeWorkerThreadProc, this, 0, nullptr);
    if (writeThread_ == nullptr) {
        lastErrorText_ = win32SerialErrorText(GetLastError(), "启动串口写入线程");
        return false;
    }
    return true;
}

void Win32SerialPort::stopWriteWorker() {
    HANDLE thread = nullptr;
    {
        WriteLock lock(writeLock_);
        writeWorkerStopRequested_ = true;
        thread = writeThread_;
    }
    if (writeWakeEvent_ != nullptr) {
        SetEvent(writeWakeEvent_);
    }
    if (thread != nullptr) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    {
        WriteLock lock(writeLock_);
        if (writeThread_ == thread) {
            writeThread_ = nullptr;
        }
        writeWorkerStopRequested_ = false;
        writeInProgress_ = false;
        std::vector<svm::transport::SerialWriteResult> cancelled = writeQueue_.cancelAllPending();
        for (const auto& result : cancelled) {
            completedWrites_.push_back(result);
        }
    }
}

void Win32SerialPort::writeWorkerLoop() {
    while (true) {
        std::optional<svm::transport::SerialWriteRequest> request;
        while (true) {
            {
                WriteLock lock(writeLock_);
                if (writeWorkerStopRequested_ && writeQueue_.empty()) {
                    return;
                }
                if (!writeQueue_.empty()) {
                    request = writeQueue_.takeNext();
                    if (request.has_value()) {
                        writeInProgress_ = true;
                    }
                    break;
                }
            }
            WaitForSingleObject(writeWakeEvent_, INFINITE);
        }

        if (!request.has_value()) {
            continue;
        }

        svm::transport::SerialWriteResult result;
        if (request->cancellationRequested()) {
            result = {
                .requestId = request->id,
                .status = svm::transport::SerialWriteResultStatus::Cancelled,
                .byteCount = request->payload.size(),
            };
        } else {
            const SerialIoResult io = writeBytesInternal(request->payload.data(), request->payload.size(), false);
            if (io.ok && io.byteCount == request->payload.size()) {
                result = {
                    .requestId = request->id,
                    .status = svm::transport::SerialWriteResultStatus::Sent,
                    .byteCount = io.byteCount,
                };
            } else if (!io.ok && isTimeoutErrorText(io.errorMessage)) {
                result = {
                    .requestId = request->id,
                    .status = svm::transport::SerialWriteResultStatus::Timeout,
                    .byteCount = io.byteCount,
                    .message = io.errorMessage,
                };
            } else {
                result = {
                    .requestId = request->id,
                    .status = svm::transport::SerialWriteResultStatus::Failed,
                    .byteCount = io.byteCount,
                    .message = io.errorMessage.empty() ? "串口写入失败。" : io.errorMessage,
                };
            }
        }

        {
            WriteLock lock(writeLock_);
            completedWrites_.push_back(std::move(result));
            writeInProgress_ = false;
            if (writeWorkerStopRequested_) {
                std::vector<svm::transport::SerialWriteResult> cancelled = writeQueue_.cancelAllPending();
                for (const auto& item : cancelled) {
                    completedWrites_.push_back(item);
                }
                break;
            }
        }
    }
}

DWORD WINAPI Win32SerialPort::writeWorkerThreadProc(void* parameter) {
    auto* self = static_cast<Win32SerialPort*>(parameter);
    if (self == nullptr) {
        return 1;
    }
    self->writeWorkerLoop();
    return 0;
}

bool Win32SerialPort::waitForReadyRead(int timeoutMs) {
    if (!isOpen()) {
        lastErrorText_ = "串口未打开，无法等待接收数据。";
        return false;
    }

    const ULONGLONG startedAt = GetTickCount64();
    const ULONGLONG safeTimeout = timeoutMs > 0 ? static_cast<ULONGLONG>(timeoutMs) : 0;
    HANDLE handle = asHandle(handle_);
    lastErrorText_.clear();

    while (true) {
        COMSTAT status = {};
        DWORD errors = 0;
        if (!ClearCommError(handle, &errors, &status)) {
            lastErrorText_ = win32SerialErrorText(GetLastError(), "查询串口接收状态");
            return false;
        }

        if (errors != 0) {
            lastErrorText_ = commStatusErrorText(errors);
            return false;
        }

        if (status.cbInQue > 0) {
            lastErrorText_.clear();
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

std::vector<std::uint8_t> Win32SerialPort::readAvailable(std::size_t maxBytes) {
    if (!isOpen()) {
        lastErrorText_ = "串口未打开，无法读取数据。";
        return {};
    }

    if (maxBytes == 0) {
        lastErrorText_.clear();
        return {};
    }

    HANDLE handle = asHandle(handle_);
    COMSTAT status = {};
    DWORD errors = 0;
    if (!ClearCommError(handle, &errors, &status)) {
        lastErrorText_ = win32SerialErrorText(GetLastError(), "查询串口接收状态");
        return {};
    }

    if (errors != 0) {
        lastErrorText_ = commStatusErrorText(errors);
        return {};
    }

    if (status.cbInQue == 0) {
        lastErrorText_.clear();
        return {};
    }

    const DWORD bytesToRead = static_cast<DWORD>(std::min<std::size_t>(
        maxBytes,
        std::min<std::size_t>(options_.readBufferSize, status.cbInQue)));
    std::vector<std::uint8_t> buffer(bytesToRead);
    DWORD bytesRead = 0;
    if (!ReadFile(handle, buffer.data(), bytesToRead, &bytesRead, nullptr)) {
        lastErrorText_ = win32SerialErrorText(GetLastError(), "读取串口");
        return {};
    }

    buffer.resize(bytesRead);
    lastErrorText_.clear();
    return buffer;
}

} // namespace svm::win32

#endif
