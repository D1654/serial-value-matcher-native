#pragma once

#include "transport/serial_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace svm::transport {

struct SerialWriteQueueSnapshot;

class SerialByteStream {
public:
    virtual ~SerialByteStream() = default;

    virtual SerialTerminalResult writeBytes(
        std::vector<std::uint8_t> payload,
        SerialDeadline deadline = {}) = 0;
    virtual SerialReadResult readAvailable(
        std::size_t maxBytes,
        SerialDeadline deadline = {}) = 0;
};

class SerialWriteScheduler {
public:
    virtual ~SerialWriteScheduler() = default;

    virtual SerialWriteAdmissionResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        SerialDeadline deadline = {}) = 0;
    virtual std::vector<SerialTerminalResult> cancelPendingWrites() = 0;
    virtual std::vector<SerialTerminalResult> takeCompletedWrites() = 0;
    virtual SerialWriteQueueSnapshot writeQueueSnapshot() const = 0;
};

class SerialSession {
public:
    virtual ~SerialSession() = default;

    virtual SerialOperationResult open(SerialOpenOptions options) = 0;
    virtual SerialOperationResult close() = 0;
    virtual SerialSessionSnapshot snapshot() const = 0;
    virtual SerialOperationResult setDataTerminalReady(bool enabled) = 0;
    virtual SerialOperationResult setRequestToSend(bool enabled) = 0;
    virtual SerialByteStream& byteStream() noexcept = 0;
    virtual SerialWriteScheduler& writeScheduler() noexcept = 0;
};

} // namespace svm::transport
