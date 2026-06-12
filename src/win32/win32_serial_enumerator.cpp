#include "win32/win32_serial_enumerator.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <cwchar>
#include <map>
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

std::wstring queryDeviceProperty(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData, DWORD property) {
    DWORD requiredBytes = 0;
    if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, property, nullptr, nullptr, 0, &requiredBytes)
        || GetLastError() != ERROR_INSUFFICIENT_BUFFER
        || requiredBytes == 0) {
        return {};
    }

    std::vector<BYTE> buffer(requiredBytes + sizeof(wchar_t));
    if (!SetupDiGetDeviceRegistryPropertyW(
            deviceInfoSet,
            &deviceInfoData,
            property,
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr)) {
        return {};
    }
    const auto* text = reinterpret_cast<const wchar_t*>(buffer.data());
    return std::wstring(text);
}

std::wstring queryDevicePortName(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData) {
    HKEY key = SetupDiOpenDevRegKey(
        deviceInfoSet,
        &deviceInfoData,
        DICS_FLAG_GLOBAL,
        0,
        DIREG_DEV,
        KEY_READ);
    if (key == INVALID_HANDLE_VALUE) {
        return {};
    }

    wchar_t buffer[128] = {};
    DWORD type = 0;
    DWORD bytes = sizeof(buffer);
    const LSTATUS status = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ || buffer[0] == L'\0') {
        return {};
    }
    return buffer;
}

std::string friendlyDescription(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData) {
    std::wstring friendly = queryDeviceProperty(deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME);
    if (friendly.empty()) {
        friendly = queryDeviceProperty(deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC);
    }
    return wideToUtf8(friendly);
}

void appendSetupApiPorts(std::map<std::string, SerialPortDescriptor>& portsByName) {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return;
    }

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA deviceInfoData = {};
        deviceInfoData.cbSize = sizeof(deviceInfoData);
        if (!SetupDiEnumDeviceInfo(deviceInfoSet, index, &deviceInfoData)) {
            break;
        }

        const std::string portName = wideToUtf8(queryDevicePortName(deviceInfoSet, deviceInfoData));
        if (!isLikelyComPortName(portName)) {
            continue;
        }

        SerialPortDescriptor descriptor;
        descriptor.portName = normalizedComPortName(portName);
        descriptor.devicePath = makeWin32DevicePath(descriptor.portName);
        descriptor.description = friendlyDescription(deviceInfoSet, deviceInfoData);
        if (descriptor.description.empty()) {
            descriptor.description = "Win32 串口设备";
        }
        portsByName[descriptor.portName] = std::move(descriptor);
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
}

void appendDosDevicePorts(std::map<std::string, SerialPortDescriptor>& portsByName) {
    for (const std::wstring& deviceName : queryDeviceNames()) {
        const std::string portName = wideToUtf8(deviceName);
        if (!isLikelyComPortName(portName)) {
            continue;
        }

        const std::string normalized = normalizedComPortName(portName);
        if (portsByName.find(normalized) != portsByName.end()) {
            continue;
        }

        SerialPortDescriptor descriptor;
        descriptor.portName = normalized;
        descriptor.devicePath = makeWin32DevicePath(descriptor.portName);
        const std::string target = queryDeviceTarget(deviceName);
        descriptor.description = target.empty() ? "Win32 串口设备" : "Win32 串口设备：" + target;
        portsByName[descriptor.portName] = std::move(descriptor);
    }
}

} // namespace

std::vector<SerialPortDescriptor> Win32SerialEnumerator::availablePorts() {
    std::map<std::string, SerialPortDescriptor> portsByName;
    appendSetupApiPorts(portsByName);
    appendDosDevicePorts(portsByName);

    std::vector<SerialPortDescriptor> ports;
    ports.reserve(portsByName.size());
    for (auto& item : portsByName) {
        ports.push_back(std::move(item.second));
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
