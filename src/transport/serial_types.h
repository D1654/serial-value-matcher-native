#pragma once

#include <cstddef>
#include <string>

namespace svm::transport {

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

struct SerialIoResult {
    bool ok = true;
    std::size_t byteCount = 0;
    std::string errorMessage;
};

} // namespace svm::transport
