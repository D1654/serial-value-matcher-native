#pragma once

#include "core/modbus_scan_executor_core.h"
#include "transport/serial_session.h"

#include <functional>
#include <string>

namespace svm::transport {

struct SerialRtuTransportOptions {
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
    core::Text endpoint;
    std::function<bool(SerialSessionGeneration)> generationIsCurrent;
    std::function<bool()> shouldCancel;
    std::function<core::Text()> nowUtc;
    std::function<void(bool tx, const core::ByteBuffer& frame)> onFrame;
    core::Text timeoutErrorMessage;
};

class SerialRtuTransport final : public core::modbus::RtuTransport {
public:
    SerialRtuTransport(SerialByteStream& byteStream, SerialRtuTransportOptions options = {});

    core::modbus::RtuTransportExchange exchange(core::ByteSpan requestFrame, int responseTimeoutMs) override;

    bool serialFailed() const noexcept;
    bool cancelObserved() const noexcept;
    const core::Text& lastErrorMessage() const noexcept;

private:
    bool cancellationRequested() const;
    bool generationCurrent() const;
    bool resultMatchesGeneration(
        const SerialOperationResult& result,
        SerialOperationKind kind) const noexcept;
    core::Text timestamp() const;
    void markCancelled(core::modbus::RtuTransportExchange& exchange);
    void markGenerationChanged(core::modbus::RtuTransportExchange& exchange);
    void markTransportFailure(core::modbus::RtuTransportExchange& exchange, core::Text message);
    core::Text operationFailureMessage(
        const SerialOperationResult& result,
        bool write) const;

    SerialByteStream& byteStream_;
    SerialRtuTransportOptions options_;
    bool serialFailed_ = false;
    bool cancelObserved_ = false;
    core::Text lastErrorMessage_;
};

} // namespace svm::transport
