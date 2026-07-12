#include "transport/serial_rtu_transport.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace svm::transport {
namespace {

constexpr const char* kCancelledMessage = "扫描已取消。";
constexpr const char* kEmptyRequestMessage = "Modbus 请求为空。";
constexpr const char* kPartialWriteMessage = "Modbus 请求发送不完整。";
constexpr const char* kTimeoutMessage = "等待 Modbus 响应超时。";

std::size_t expectedNormalResponseLength(core::ByteSpan requestFrame) {
    if (requestFrame.size() >= 6) {
        const std::size_t quantity = (static_cast<std::size_t>(requestFrame[4]) << 8U)
            | static_cast<std::size_t>(requestFrame[5]);
        if (quantity > 0 && quantity <= 125) {
            return 5 + quantity * 2;
        }
    }
    return 5;
}

bool responseLooksComplete(const core::ByteBuffer& response, std::size_t expectedNormalBytes) {
    if (response.size() >= 5 && (response[1] & 0x80U) != 0) {
        return true;
    }
    return response.size() >= expectedNormalBytes;
}

} // namespace

SerialRtuTransport::SerialRtuTransport(SerialTransport& serialTransport, SerialRtuTransportOptions options)
    : serialTransport_(serialTransport),
      options_(std::move(options)) {
}

core::modbus::RtuTransportExchange SerialRtuTransport::exchange(core::ByteSpan requestFrame, int responseTimeoutMs) {
    core::modbus::RtuTransportExchange exchange;
    exchange.requestFrame.assign(requestFrame.begin(), requestFrame.end());
    exchange.endpoint = serialTransport_.endpoint();
    exchange.sentAtUtc = timestamp();

    if (cancellationRequested()) {
        cancelObserved_ = true;
        exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
        exchange.errorMessage = kCancelledMessage;
        exchange.receivedAtUtc = timestamp();
        return exchange;
    }
    if (requestFrame.empty()) {
        exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
        exchange.errorMessage = kEmptyRequestMessage;
        return exchange;
    }

    const core::ByteBuffer request(requestFrame.begin(), requestFrame.end());
    const SerialIoResult writeResult = serialTransport_.writeBytes(request);
    if (!writeResult.ok) {
        markTransportFailure(exchange, writeResult.errorMessage);
        return exchange;
    }
    if (writeResult.byteCount != request.size()) {
        markTransportFailure(exchange, kPartialWriteMessage);
        return exchange;
    }
    if (options_.onFrame) {
        options_.onFrame(true, request);
    }

    const std::size_t expectedNormalBytes = expectedNormalResponseLength(requestFrame);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, responseTimeoutMs));
    core::ByteBuffer response;
    exchange.status = core::modbus::RtuTransportExchangeStatus::Timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (cancellationRequested()) {
            cancelObserved_ = true;
            exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
            exchange.errorMessage = kCancelledMessage;
            break;
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            break;
        }
        const int waitMs = static_cast<int>(std::min<long long>(remaining, 50));
        if (!serialTransport_.waitForReadyRead(waitMs)) {
            const core::Text error = serialTransport_.lastErrorText();
            if (!error.empty()) {
                markTransportFailure(exchange, error);
                break;
            }
            continue;
        }

        const auto chunk = serialTransport_.readAvailable(260);
        if (chunk.empty()) {
            const core::Text error = serialTransport_.lastErrorText();
            if (!error.empty()) {
                markTransportFailure(exchange, error);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        response.insert(response.end(), chunk.begin(), chunk.end());
        if (responseLooksComplete(response, expectedNormalBytes)) {
            exchange.status = core::modbus::RtuTransportExchangeStatus::Success;
            break;
        }
    }

    exchange.receivedAtUtc = timestamp();
    exchange.responseFrame = std::move(response);
    if (!exchange.responseFrame.empty() && options_.onFrame) {
        options_.onFrame(false, exchange.responseFrame);
    }
    if (exchange.status == core::modbus::RtuTransportExchangeStatus::Success
        || exchange.status == core::modbus::RtuTransportExchangeStatus::TransportError) {
        return exchange;
    }

    exchange.status = core::modbus::RtuTransportExchangeStatus::Timeout;
    exchange.errorMessage = options_.timeoutErrorMessage.empty() ? kTimeoutMessage : options_.timeoutErrorMessage;
    return exchange;
}

bool SerialRtuTransport::serialFailed() const noexcept {
    return serialFailed_;
}

bool SerialRtuTransport::cancelObserved() const noexcept {
    return cancelObserved_;
}

const core::Text& SerialRtuTransport::lastErrorMessage() const noexcept {
    return lastErrorMessage_;
}

bool SerialRtuTransport::cancellationRequested() const {
    return options_.shouldCancel && options_.shouldCancel();
}

core::Text SerialRtuTransport::timestamp() const {
    return options_.nowUtc ? options_.nowUtc() : core::Text{};
}

void SerialRtuTransport::markTransportFailure(core::modbus::RtuTransportExchange& exchange, core::Text message) {
    serialFailed_ = true;
    lastErrorMessage_ = std::move(message);
    exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
    exchange.errorMessage = lastErrorMessage_;
}

} // namespace svm::transport
