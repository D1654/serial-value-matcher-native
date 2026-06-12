#pragma once

#include "win32/win32_serial_types.h"

#if defined(_WIN32)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace svm::win32 {

class Win32SerialPort final {
public:
    Win32SerialPort() = default;
    ~Win32SerialPort();

    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;
    Win32SerialPort(Win32SerialPort&& other) noexcept;
    Win32SerialPort& operator=(Win32SerialPort&& other) noexcept;

    bool open(SerialOpenOptions options);
    void close();
    bool isOpen() const noexcept;

    std::string endpoint() const;
    std::string lastErrorText() const;

    SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload);
    SerialIoResult writeBytes(const std::uint8_t* payload, std::size_t size);
    bool waitForReadyRead(int timeoutMs);
    std::vector<std::uint8_t> readAvailable(std::size_t maxBytes);

private:
    void* handle_ = nullptr;
    SerialOpenOptions options_;
    std::string lastErrorText_;
};

} // namespace svm::win32

#endif
