#include "win32/win32_serial_enumerator.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace svm::win32 {
namespace {

std::string lossyAsciiFromWide(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t ch : value) {
        result.push_back(ch >= 0 && ch <= 0x7F ? static_cast<char>(ch) : '?');
    }
    return result;
}

std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return lossyAsciiFromWide(value);
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::vector<std::wstring> parseMultiString(const std::vector<wchar_t>& buffer, DWORD length) {
    std::vector<std::wstring> values;
    std::size_t index = 0;
    while (index < length && buffer[index] != L'\0') {
        const wchar_t* start = buffer.data() + index;
        const std::size_t valueLength = std::wcslen(start);
        values.emplace_back(start, valueLength);
        index += valueLength + 1;
    }
    return values;
}

std::vector<std::wstring> queryDeviceNames() {
    DWORD size = 32768;
    for (int attempt = 0; attempt < 4; ++attempt) {
        std::vector<wchar_t> buffer(size);
        const DWORD length = QueryDosDeviceW(nullptr, buffer.data(), size);
        if (length != 0) {
            return parseMultiString(buffer, length);
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return {};
        }
        size *= 2;
    }
    return {};
}

std::string queryDeviceTarget(const std::wstring& deviceName) {
    std::vector<wchar_t> buffer(1024);
    const DWORD length = QueryDosDeviceW(deviceName.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || buffer.empty()) {
        return {};
    }

    return wideToUtf8(std::wstring_view(buffer.data(), std::wcslen(buffer.data())));
}

} // namespace

std::vector<SerialPortDescriptor> Win32SerialEnumerator::availablePorts() {
    std::vector<SerialPortDescriptor> ports;
    for (const std::wstring& deviceName : queryDeviceNames()) {
        const std::string portName = wideToUtf8(deviceName);
        if (!isLikelyComPortName(portName)) {
            continue;
        }

        SerialPortDescriptor descriptor;
        descriptor.portName = normalizedComPortName(portName);
        descriptor.devicePath = makeWin32DevicePath(descriptor.portName);
        const std::string target = queryDeviceTarget(deviceName);
        descriptor.description = target.empty() ? "Win32 串口设备" : "Win32 串口设备：" + target;
        ports.push_back(std::move(descriptor));
    }

    std::sort(ports.begin(), ports.end(), [](const SerialPortDescriptor& left, const SerialPortDescriptor& right) {
        const int leftNumber = comPortNumber(left.portName);
        const int rightNumber = comPortNumber(right.portName);
        if (leftNumber != rightNumber) {
            return leftNumber < rightNumber;
        }
        return left.portName < right.portName;
    });

    return ports;
}

} // namespace svm::win32

#endif
