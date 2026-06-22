#pragma once

#include "win32/win32_serial_types.h"

#include <string>
#include <string_view>

namespace svm::win32 {

std::string nativeSerialParityKey(SerialParity parity);
SerialParity nativeSerialParityFromKey(std::string_view key);

std::string nativeSerialStopBitsKey(SerialStopBits stopBits);
SerialStopBits nativeSerialStopBitsFromKey(std::string_view key);

std::string nativeSerialFlowControlKey(SerialFlowControl flowControl);
SerialFlowControl nativeSerialFlowControlFromKey(std::string_view key);

} // namespace svm::win32
