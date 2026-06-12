#include "win32/win32_serial_types.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace svm::win32 {
namespace {

bool asciiEqualIgnoreCase(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
}

bool startsWithIgnoreCase(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (!asciiEqualIgnoreCase(value[index], prefix[index])) {
            return false;
        }
    }
    return true;
}

bool isAsciiDigitString(std::string_view value) {
    return !value.empty()
        && std::all_of(value.begin(), value.end(), [](char ch) {
               return std::isdigit(static_cast<unsigned char>(ch)) != 0;
           });
}

std::string withOperation(std::string_view operation, std::string message, unsigned long errorCode) {
    std::ostringstream output;
    if (!operation.empty()) {
        output << operation << "失败：";
    }
    output << message << " Win32 错误码：" << errorCode << "。";
    return output.str();
}

} // namespace

std::string trimPortName(std::string_view portName) {
    std::size_t first = 0;
    while (first < portName.size() && std::isspace(static_cast<unsigned char>(portName[first])) != 0) {
        ++first;
    }

    std::size_t last = portName.size();
    while (last > first && std::isspace(static_cast<unsigned char>(portName[last - 1])) != 0) {
        --last;
    }

    return std::string(portName.substr(first, last - first));
}

std::string normalizedComPortName(std::string_view portName) {
    const std::string stripped = stripWin32DevicePrefix(trimPortName(portName));
    if (!startsWithIgnoreCase(stripped, "COM")) {
        return stripped;
    }

    std::string normalized = "COM";
    normalized.append(stripped.substr(3));
    return normalized;
}

int comPortNumber(std::string_view portName) {
    const std::string normalized = normalizedComPortName(portName);
    if (!startsWithIgnoreCase(normalized, "COM") || !isAsciiDigitString(std::string_view(normalized).substr(3))) {
        return -1;
    }

    const std::string_view digits = std::string_view(normalized).substr(3);
    long long value = 0;
    for (char digit : digits) {
        value = value * 10 + (digit - '0');
        if (value > std::numeric_limits<int>::max()) {
            return -1;
        }
    }

    return value > 0 ? static_cast<int>(value) : -1;
}

bool isWin32DevicePath(std::string_view portName) {
    const std::string trimmed = trimPortName(portName);
    return startsWithIgnoreCase(trimmed, R"(\\.\)") || startsWithIgnoreCase(trimmed, R"(\\?\)");
}

bool isLikelyComPortName(std::string_view portName) {
    return comPortNumber(portName) > 0;
}

std::string makeWin32DevicePath(std::string_view portName) {
    const std::string trimmed = trimPortName(portName);
    if (trimmed.empty() || isWin32DevicePath(trimmed)) {
        return trimmed;
    }

    return R"(\\.\)" + normalizedComPortName(trimmed);
}

std::string stripWin32DevicePrefix(std::string_view portName) {
    const std::string trimmed = trimPortName(portName);
    if (startsWithIgnoreCase(trimmed, R"(\\.\)") || startsWithIgnoreCase(trimmed, R"(\\?\)")) {
        return trimmed.substr(4);
    }
    return trimmed;
}

std::string serialParityName(SerialParity parity) {
    switch (parity) {
    case SerialParity::None:
        return "无校验";
    case SerialParity::Odd:
        return "奇校验";
    case SerialParity::Even:
        return "偶校验";
    case SerialParity::Mark:
        return "标记校验";
    case SerialParity::Space:
        return "空格校验";
    }
    return "未知校验";
}

std::string serialStopBitsName(SerialStopBits stopBits) {
    switch (stopBits) {
    case SerialStopBits::One:
        return "1 位停止位";
    case SerialStopBits::OnePointFive:
        return "1.5 位停止位";
    case SerialStopBits::Two:
        return "2 位停止位";
    }
    return "未知停止位";
}

std::string serialFlowControlName(SerialFlowControl flowControl) {
    switch (flowControl) {
    case SerialFlowControl::None:
        return "无流控";
    case SerialFlowControl::HardwareRtsCts:
        return "RTS/CTS 硬件流控";
    case SerialFlowControl::SoftwareXonXoff:
        return "XON/XOFF 软件流控";
    }
    return "未知流控";
}

SerialValidationResult validateSerialOpenOptions(const SerialOpenOptions& options) {
    const std::string portName = trimPortName(options.portName);
    if (portName.empty()) {
        return {false, "未选择串口，无法打开串口设备。"};
    }

    if (!isWin32DevicePath(portName) && !isLikelyComPortName(portName)) {
        return {false, "串口名格式不正确。Windows 原生后端需要 COM1、COM10 或 \\\\.\\COM10 这样的端口名。"};
    }

    if (options.baudRate <= 0 || options.baudRate > 10000000) {
        return {false, "波特率不在可接受范围内，请输入 1 到 10000000 之间的整数。"};
    }

    if (options.dataBits < 5 || options.dataBits > 8) {
        return {false, "数据位不受支持。Windows 串口数据位应为 5、6、7 或 8。"};
    }

    if (options.readTimeoutMs < 0 || options.writeTimeoutMs < 0) {
        return {false, "串口读写超时不能为负数。"};
    }

    if (options.readBufferSize == 0 || options.readBufferSize > 1024 * 1024) {
        return {false, "串口读取缓冲区大小不合理，应在 1 字节到 1 MB 之间。"};
    }

    return {};
}

std::string win32SerialErrorText(unsigned long errorCode, std::string_view operation) {
    switch (errorCode) {
    case 2: // ERROR_FILE_NOT_FOUND
    case 3: // ERROR_PATH_NOT_FOUND
        return withOperation(operation,
            "没有找到这个串口设备。可能是设备已拔出、端口号变化，或驱动没有正确加载。请刷新端口列表后重试。",
            errorCode);
    case 5: // ERROR_ACCESS_DENIED
    case 32: // ERROR_SHARING_VIOLATION
        return withOperation(operation,
            "串口被占用或权限不足。请先关闭其他串口助手、烧录工具或后台服务，再重新连接。",
            errorCode);
    case 6: // ERROR_INVALID_HANDLE
        return withOperation(operation,
            "串口句柄已失效。设备可能已经断开，或程序正在使用一个已关闭的连接。",
            errorCode);
    case 31: // ERROR_GEN_FAILURE
    case 1117: // ERROR_IO_DEVICE
        return withOperation(operation,
            "串口驱动报告 I/O 错误。设备可能已断开、USB 转串口芯片异常，或线缆连接不稳定。",
            errorCode);
    case 87: // ERROR_INVALID_PARAMETER
        return withOperation(operation,
            "串口参数不被驱动接受。请检查波特率、数据位、校验位、停止位和流控设置。",
            errorCode);
    case 121: // ERROR_SEM_TIMEOUT
        return withOperation(operation,
            "串口操作超时。设备可能没有响应，或当前波特率、接线、流控、协议参数不匹配。",
            errorCode);
    case 995: // ERROR_OPERATION_ABORTED
        return withOperation(operation,
            "串口操作被系统取消。通常发生在设备断开、端口关闭或驱动重置时。",
            errorCode);
    case 1167: // ERROR_DEVICE_NOT_CONNECTED
        return withOperation(operation,
            "串口设备已经断开连接。请重新插拔设备并刷新端口列表。",
            errorCode);
    default:
        return withOperation(operation,
            "发生未知串口错误。请检查设备连接、驱动状态、端口占用和串口参数。",
            errorCode);
    }
}

} // namespace svm::win32
