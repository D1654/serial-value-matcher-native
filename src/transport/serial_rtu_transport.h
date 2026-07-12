#pragma once

#include "core/modbus_scan_executor_core.h"
#include "transport/serial_transport.h"

#include <functional>
#include <string>

namespace svm::transport {

struct SerialRtuTransportOptions {
    std::function<bool()> shouldCancel;
    std::function<core::Text()> nowUtc;
    std::function<void(bool tx, const core::ByteBuffer& frame)> onFrame;
    core::Text timeoutErrorMessage;
};

class SerialRtuTransport final : public core::modbus::RtuTransport {
public:
    SerialRtuTransport(SerialTransport& serialTransport, SerialRtuTransportOptions options = {});

    core::modbus::RtuTransportExchange exchange(core::ByteSpan requestFrame, int responseTimeoutMs) override;

    bool serialFailed() const noexcept;
    bool cancelObserved() const noexcept;
    const core::Text& lastErrorMessage() const noexcept;

private:
    bool cancellationRequested() const;
    core::Text timestamp() const;
    void markTransportFailure(core::modbus::RtuTransportExchange& exchange, core::Text message);

    SerialTransport& serialTransport_;
    SerialRtuTransportOptions options_;
    bool serialFailed_ = false;
    bool cancelObserved_ = false;
    core::Text lastErrorMessage_;
};

} // namespace svm::transport
