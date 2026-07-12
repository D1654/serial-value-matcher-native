#pragma once

#include "transport/serial_types.h"

#include <string>
#include <string_view>

namespace svm::win32 {

using SerialParity = transport::SerialParity;
using SerialStopBits = transport::SerialStopBits;
using SerialFlowControl = transport::SerialFlowControl;
using SerialOpenOptions = transport::SerialOpenOptions;
using SerialIoResult = transport::SerialIoResult;

struct SerialPortDescriptor {
    std::string portName;
    std::string devicePath;
    std::string description;
};

struct SerialValidationResult {
    bool ok = true;
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
