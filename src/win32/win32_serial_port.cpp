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
#include <utility>

namespace svm::win32 {
namespace {

HANDLE asHandle(void* handle) noexcept {
    return static_cast<HANDLE>(handle);
}

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
        return setLastErrorText(lastErrorText, GetLastError(), "设置 DTR 信号");
    }

    if (options.flowControl != SerialFlowControl::HardwareRtsCts
        && !EscapeCommFunction(handle, options.requestToSend ? SETRTS : CLRRTS)) {
        return setLastErrorText(lastErrorText, GetLastError(), "设置 RTS 信号");
    }

    return true;
}

} // namespace

Win32SerialPort::~Win32SerialPort() {
    close();
}

Win32SerialPort::Win32SerialPort(Win32SerialPort&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      options_(std::move(other.options_)),
      lastErrorText_(std::move(other.lastErrorText_)) {
}

Win32SerialPort& Win32SerialPort::operator=(Win32SerialPort&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        options_ = std::move(other.options_);
        lastErrorText_ = std::move(other.lastErrorText_);
    }
    return *this;
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
    HANDLE handle = asHandle(handle_);
    if (!isValidHandle(handle)) {
        handle_ = nullptr;
        return;
    }

    CloseHandle(handle);
    handle_ = nullptr;
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

SerialIoResult Win32SerialPort::writeBytes(const std::vector<std::uint8_t>& payload) {
    return writeBytes(payload.data(), payload.size());
}

SerialIoResult Win32SerialPort::writeBytes(const std::uint8_t* payload, std::size_t size) {
    if (!isOpen()) {
        lastErrorText_ = "串口未打开，无法发送数据。";
        return {false, 0, lastErrorText_};
    }

    if (size == 0) {
        lastErrorText_.clear();
        return {};
    }

    if (payload == nullptr) {
        lastErrorText_ = "待发送数据为空指针，无法写入串口。";
        return {false, 0, lastErrorText_};
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
            lastErrorText_ = win32SerialErrorText(GetLastError(), "写入串口");
            return {false, totalWritten, lastErrorText_};
        }

        totalWritten += written;
        if (written == 0) {
            lastErrorText_ = "串口驱动没有写入任何字节。设备可能已断开、缓冲区已满，或流控阻止继续发送。";
            return {false, totalWritten, lastErrorText_};
        }
    }

    lastErrorText_.clear();
    return {true, totalWritten, {}};
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
