#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace svm::win32 {

enum class SerialParity {
    None,
    Odd,
    Even,
    Mark,
    Space,
};

enum class SerialStopBits {
    One,
    OnePointFive,
    Two,
};

enum class SerialFlowControl {
    None,
    HardwareRtsCts,
    SoftwareXonXoff,
};

struct SerialOpenOptions {
    std::string portName;
    int baudRate = 115200;
    int dataBits = 8;
    SerialParity parity = SerialParity::None;
    SerialStopBits stopBits = SerialStopBits::One;
    SerialFlowControl flowControl = SerialFlowControl::None;
    bool dataTerminalReady = false;
    bool requestToSend = false;
    int readTimeoutMs = 1000;
    int writeTimeoutMs = 1000;
    std::size_t readBufferSize = 4096;
};

struct SerialPortDescriptor {
    std::string portName;
    std::string devicePath;
    std::string description;
};

struct SerialValidationResult {
    bool ok = true;
    std::string errorMessage;
};

struct SerialIoResult {
    bool ok = true;
    std::size_t byteCount = 0;
    std::string errorMessage;
};

std::string trimPortName(std::string_view portName);
std::string normalizedComPortName(std::string_view portName);
int comPortNumber(std::string_view portName);
bool isWin32DevicePath(std::string_view portName);
bool isLikelyComPortName(std::string_view portName);
std::string makeWin32DevicePath(std::string_view portName);
std::string stripWin32DevicePrefix(std::string_view portName);

std::string serialParityName(SerialParity parity);
std::string serialStopBitsName(SerialStopBits stopBits);
std::string serialFlowControlName(SerialFlowControl flowControl);

SerialValidationResult validateSerialOpenOptions(const SerialOpenOptions& options);
std::string win32SerialErrorText(unsigned long errorCode, std::string_view operation = {});

} // namespace svm::win32
