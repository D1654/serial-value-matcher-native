#include "win32/native_serial_profile_codec.h"

namespace svm::win32 {

std::string nativeSerialParityKey(SerialParity parity) {
    switch (parity) {
    case SerialParity::None:
        return "None";
    case SerialParity::Odd:
        return "Odd";
    case SerialParity::Even:
        return "Even";
    case SerialParity::Mark:
        return "Mark";
    case SerialParity::Space:
        return "Space";
    }
    return "None";
}

SerialParity nativeSerialParityFromKey(std::string_view key) {
    if (key == "Odd") {
        return SerialParity::Odd;
    }
    if (key == "Even") {
        return SerialParity::Even;
    }
    if (key == "Mark") {
        return SerialParity::Mark;
    }
    if (key == "Space") {
        return SerialParity::Space;
    }
    return SerialParity::None;
}

std::string nativeSerialStopBitsKey(SerialStopBits stopBits) {
    switch (stopBits) {
    case SerialStopBits::One:
        return "One";
    case SerialStopBits::OnePointFive:
        return "OnePointFive";
    case SerialStopBits::Two:
        return "Two";
    }
    return "One";
}

SerialStopBits nativeSerialStopBitsFromKey(std::string_view key) {
    if (key == "OnePointFive") {
        return SerialStopBits::OnePointFive;
    }
    if (key == "Two") {
        return SerialStopBits::Two;
    }
    return SerialStopBits::One;
}

std::string nativeSerialFlowControlKey(SerialFlowControl flowControl) {
    switch (flowControl) {
    case SerialFlowControl::None:
        return "None";
    case SerialFlowControl::HardwareRtsCts:
        return "Hardware";
    case SerialFlowControl::SoftwareXonXoff:
        return "Software";
    }
    return "None";
}

SerialFlowControl nativeSerialFlowControlFromKey(std::string_view key) {
    if (key == "Hardware") {
        return SerialFlowControl::HardwareRtsCts;
    }
    if (key == "Software") {
        return SerialFlowControl::SoftwareXonXoff;
    }
    return SerialFlowControl::None;
}

} // namespace svm::win32
