#include "transport/serial_rtu_transport.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

namespace svm::transport {
namespace {

constexpr const char* kCancelledMessage = "扫描已取消。";
constexpr const char* kEmptyRequestMessage = "Modbus 请求为空。";
constexpr const char* kPartialWriteMessage = "Modbus 请求发送不完整。";
constexpr const char* kTimeoutMessage = "等待 Modbus 响应超时。";
constexpr const char* kSessionChangedMessage = "串口会话已变化，Modbus 扫描已停止。";
constexpr std::size_t kMinimumRtuFrameBytes = 4;
constexpr std::size_t kMaxRtuFrameBytes = 260;

std::optional<std::size_t> responseFrameLength(
    const core::ByteBuffer& response,
    std::size_t& nextCandidateBytes) {
    if (response.size() < 2) {
        return std::nullopt;
    }
    if ((response[1] & 0x80U) != 0) {
        return 5;
    }
    if (response[1] == 0x03 || response[1] == 0x04) {
        if (response.size() < 3) {
            return std::nullopt;
        }
        return 5 + static_cast<std::size_t>(response[2]);
    }
    while (nextCandidateBytes <= response.size()) {
        const core::ByteSpan candidate(response.data(), nextCandidateBytes);
        if (core::modbus::validateRtuFrame(candidate).ok) {
            return nextCandidateBytes;
        }
        ++nextCandidateBytes;
    }
    return std::nullopt;
}

} // namespace

SerialRtuTransport::SerialRtuTransport(SerialByteStream& byteStream, SerialRtuTransportOptions options)
    : byteStream_(byteStream),
      options_(std::move(options)) {
}

core::modbus::RtuTransportExchange SerialRtuTransport::exchange(core::ByteSpan requestFrame, int responseTimeoutMs) {
    core::modbus::RtuTransportExchange exchange;
    exchange.requestFrame.assign(requestFrame.begin(), requestFrame.end());
    exchange.endpoint = options_.endpoint;
    exchange.sentAtUtc = timestamp();

    if (cancellationRequested()) {
        markCancelled(exchange);
        return exchange;
    }
    if (!generationCurrent()) {
        markGenerationChanged(exchange);
        return exchange;
    }
    if (requestFrame.empty()) {
        exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
        exchange.errorMessage = kEmptyRequestMessage;
        return exchange;
    }

    const auto timeout = std::chrono::milliseconds(std::max(0, responseTimeoutMs));
    const auto deadlineAt = std::chrono::steady_clock::now() + timeout;
    const SerialDeadline deadline{.expiresAt = deadlineAt};
    const core::ByteBuffer request(requestFrame.begin(), requestFrame.end());
    const SerialTerminalResult writeResult = byteStream_.writeBytes(request, deadline);
    if (!resultMatchesGeneration(writeResult, SerialOperationKind::Write)) {
        markGenerationChanged(exchange);
        return exchange;
    }
    if (cancellationRequested()) {
        if (options_.onIoEvidence) {
            const std::size_t transmittedBytes = std::min(writeResult.byteCount, request.size());
            options_.onIoEvidence(
                writeResult.succeeded()
                    ? core::ByteBuffer(request.begin(), request.begin() + transmittedBytes)
                    : core::ByteBuffer{},
                writeResult);
        }
        markCancelled(exchange);
        return exchange;
    }
    if (!generationCurrent()
        && writeResult.status != SerialOperationStatus::Cancelled
        && writeResult.status != SerialOperationStatus::Disconnected) {
        markGenerationChanged(exchange);
        return exchange;
    }
    if (writeResult.status == SerialOperationStatus::Cancelled) {
        if (options_.onIoEvidence) {
            options_.onIoEvidence({}, writeResult);
        }
        markCancelled(exchange);
        return exchange;
    }
    if (writeResult.status == SerialOperationStatus::RejectedClosed) {
        if (options_.onIoEvidence) {
            options_.onIoEvidence({}, writeResult);
        }
        markGenerationChanged(exchange);
        return exchange;
    }
    if (!writeResult.succeeded()) {
        if (options_.onIoEvidence) {
            options_.onIoEvidence({}, writeResult);
        }
        markTransportFailure(exchange, writeResult, operationFailureMessage(writeResult, true));
        return exchange;
    }
    if (!generationCurrent()) {
        markGenerationChanged(exchange);
        return exchange;
    }
    if (writeResult.byteCount != request.size()) {
        SerialOperationResult partialWrite = writeResult;
        partialWrite.status = SerialOperationStatus::Failed;
        partialWrite.error.category = SerialErrorCategory::IoFailure;
        partialWrite.error.byteCount = partialWrite.byteCount;
        if (options_.onIoEvidence) {
            const std::size_t transmittedBytes = std::min(partialWrite.byteCount, request.size());
            options_.onIoEvidence(
                core::ByteBuffer(request.begin(), request.begin() + transmittedBytes),
                partialWrite);
        }
        markTransportFailure(exchange, std::move(partialWrite), kPartialWriteMessage);
        return exchange;
    }
    if (options_.onIoEvidence) {
        options_.onIoEvidence(request, writeResult);
    }

    core::ByteBuffer response;
    std::optional<std::size_t> completeFrameBytes;
    std::size_t nextCandidateBytes = kMinimumRtuFrameBytes;
    bool discardResponse = false;
    exchange.status = core::modbus::RtuTransportExchangeStatus::Timeout;

    while (std::chrono::steady_clock::now() < deadlineAt) {
        if (cancellationRequested()) {
            markCancelled(exchange);
            break;
        }
        if (!generationCurrent()) {
            discardResponse = true;
            markGenerationChanged(exchange);
            break;
        }

        const std::size_t remainingFrameBytes = kMaxRtuFrameBytes - response.size();
        SerialReadResult read = byteStream_.readAvailable(remainingFrameBytes, deadline);
        if (!resultMatchesGeneration(read.operation, SerialOperationKind::Read)) {
            discardResponse = true;
            markGenerationChanged(exchange);
            break;
        }
        if (cancellationRequested()) {
            if (options_.onIoEvidence) {
                options_.onIoEvidence(
                    read.operation.succeeded() ? read.bytes : core::ByteBuffer{},
                    read.operation);
            }
            markCancelled(exchange);
            break;
        }
        if (!generationCurrent()
            && read.operation.status != SerialOperationStatus::Cancelled
            && read.operation.status != SerialOperationStatus::Disconnected) {
            discardResponse = true;
            markGenerationChanged(exchange);
            break;
        }
        if (read.operation.status == SerialOperationStatus::Cancelled) {
            if (options_.onIoEvidence) {
                options_.onIoEvidence({}, read.operation);
            }
            markCancelled(exchange);
            break;
        }
        if (read.operation.status == SerialOperationStatus::RejectedClosed) {
            if (options_.onIoEvidence) {
                options_.onIoEvidence({}, read.operation);
            }
            discardResponse = true;
            markGenerationChanged(exchange);
            break;
        }
        if (read.operation.status == SerialOperationStatus::Timeout) {
            if (options_.onIoEvidence) {
                options_.onIoEvidence({}, read.operation);
            }
            break;
        }
        if (!read.operation.succeeded()) {
            if (options_.onIoEvidence) {
                options_.onIoEvidence({}, read.operation);
            }
            markTransportFailure(exchange, read.operation, operationFailureMessage(read.operation, false));
            break;
        }
        if (!generationCurrent()) {
            discardResponse = true;
            markGenerationChanged(exchange);
            break;
        }
        if (read.bytes.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (options_.onIoEvidence) {
            options_.onIoEvidence(read.bytes, read.operation);
        }
        const std::size_t acceptedBytes = std::min(read.bytes.size(), remainingFrameBytes);
        response.insert(response.end(), read.bytes.begin(), read.bytes.begin() + acceptedBytes);
        const std::optional<std::size_t> frameBytes = responseFrameLength(response, nextCandidateBytes);
        if (frameBytes.has_value() && response.size() >= *frameBytes) {
            completeFrameBytes = frameBytes;
            exchange.status = core::modbus::RtuTransportExchangeStatus::Success;
            break;
        }
        if (response.size() == kMaxRtuFrameBytes) {
            completeFrameBytes = response.size();
            exchange.status = core::modbus::RtuTransportExchangeStatus::Success;
            break;
        }
    }

    exchange.receivedAtUtc = timestamp();
    if (!generationCurrent()) {
        discardResponse = true;
        if (exchange.status != core::modbus::RtuTransportExchangeStatus::TransportError) {
            markGenerationChanged(exchange);
        }
    }
    if (discardResponse) {
        response.clear();
    }
    if (completeFrameBytes.has_value() && response.size() > *completeFrameBytes) {
        response.resize(*completeFrameBytes);
    }
    exchange.responseFrame = std::move(response);
    if (!generationCurrent()) {
        exchange.responseFrame.clear();
        if (exchange.status != core::modbus::RtuTransportExchangeStatus::TransportError) {
            markGenerationChanged(exchange);
        }
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
    return serialFailure_.has_value();
}

bool SerialRtuTransport::cancelObserved() const noexcept {
    return cancelObserved_;
}

const SerialOperationResult* SerialRtuTransport::serialFailure() const noexcept {
    return serialFailure_ ? &*serialFailure_ : nullptr;
}

const core::Text& SerialRtuTransport::lastErrorMessage() const noexcept {
    return lastErrorMessage_;
}

bool SerialRtuTransport::cancellationRequested() const {
    return options_.shouldCancel && options_.shouldCancel();
}

bool SerialRtuTransport::generationCurrent() const {
    return options_.generation != kUnassignedSerialSessionGeneration
        && (!options_.generationIsCurrent || options_.generationIsCurrent(options_.generation));
}

bool SerialRtuTransport::resultMatchesGeneration(
    const SerialOperationResult& result,
    SerialOperationKind kind) const noexcept {
    return result.operation.kind == kind
        && result.operation.generation == options_.generation;
}

core::Text SerialRtuTransport::timestamp() const {
    return options_.nowUtc ? options_.nowUtc() : core::Text{};
}

void SerialRtuTransport::markCancelled(core::modbus::RtuTransportExchange& exchange) {
    cancelObserved_ = true;
    exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
    exchange.errorMessage = kCancelledMessage;
    exchange.receivedAtUtc = timestamp();
}

void SerialRtuTransport::markGenerationChanged(core::modbus::RtuTransportExchange& exchange) {
    cancelObserved_ = true;
    exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
    exchange.errorMessage = kSessionChangedMessage;
    exchange.receivedAtUtc = timestamp();
}

void SerialRtuTransport::markTransportFailure(
    core::modbus::RtuTransportExchange& exchange,
    SerialOperationResult result,
    core::Text message) {
    serialFailure_ = std::move(result);
    lastErrorMessage_ = std::move(message);
    exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
    exchange.errorMessage = lastErrorMessage_;
}

core::Text SerialRtuTransport::operationFailureMessage(
    const SerialOperationResult& result,
    bool write) const {
    core::Text message;
    switch (result.status) {
    case SerialOperationStatus::Timeout:
        message = write ? "Modbus 请求发送超时。" : kTimeoutMessage;
        break;
    case SerialOperationStatus::Cancelled:
        message = kCancelledMessage;
        break;
    case SerialOperationStatus::Disconnected:
        message = "串口设备已断开。";
        break;
    case SerialOperationStatus::RejectedClosed:
        message = "串口会话已关闭。";
        break;
    case SerialOperationStatus::RejectedInvalid:
    case SerialOperationStatus::RejectedFull:
        message = write ? "Modbus 请求未被串口接受。" : "Modbus 读取请求无效。";
        break;
    case SerialOperationStatus::Failed:
        message = write ? "Modbus 请求发送失败。" : "读取 Modbus 响应失败。";
        break;
    case SerialOperationStatus::Accepted:
    case SerialOperationStatus::Succeeded:
        message = write ? "Modbus 请求发送状态无效。" : "Modbus 读取状态无效。";
        break;
    }
    if (result.error.nativeCode != 0) {
        message += " (native=" + std::to_string(result.error.nativeCode) + ")";
    }
    return message;
}

} // namespace svm::transport
