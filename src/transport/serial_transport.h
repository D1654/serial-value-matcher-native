#pragma once

#include "transport/serial_types.h"
#include "transport/serial_write_queue.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace svm::transport {

// The native application owns one serial transport instance. Protocol workers
// borrow this contract and never own the underlying device handle.
class SerialTransport : public SerialWritePort {
public:
    virtual ~SerialTransport() = default;

    virtual bool open(SerialOpenOptions options) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const noexcept = 0;

    virtual std::string endpoint() const = 0;
    virtual std::string lastErrorText() const = 0;
    virtual bool usesHardwareRtsCts() const noexcept = 0;

    virtual bool setDataTerminalReady(bool enabled) = 0;
    virtual bool setRequestToSend(bool enabled) = 0;
    virtual SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload) = 0;
    virtual SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) override = 0;
    virtual std::vector<SerialWriteResult> cancelPendingWrites() = 0;
    virtual std::vector<SerialWriteResult> takeCompletedWrites() = 0;
    virtual SerialWriteQueueSnapshot writeQueueSnapshot() const = 0;
    virtual bool waitForReadyRead(int timeoutMs) = 0;
    virtual std::vector<std::uint8_t> readAvailable(std::size_t maxBytes) = 0;
};

} // namespace svm::transport
