#include "transport/serial_write_queue.h"

#include <algorithm>
#include <utility>

namespace svm::transport {
namespace {

constexpr const char* kEmptyPayloadMessage = "串口写入请求没有 payload。";
constexpr const char* kInvalidTimeoutMessage = "串口写入请求超时时间必须大于 0 ms。";
constexpr const char* kQueueFullMessage = "串口写入队列已满，请等待前序请求完成。";
constexpr const char* kRequestNotFoundMessage = "未找到待取消的串口写入请求。";
constexpr const char* kNoPendingRequestMessage = "串口写入队列没有待完成请求。";
constexpr const char* kPartialWriteMessage = "串口写入字节数不完整。";

} // namespace

bool SerialWriteCancellationToken::valid() const noexcept {
    return requestId != 0;
}

bool SerialWriteRequest::cancellationRequested() const noexcept {
    return cancellationState == SerialWriteCancellationState::Requested;
}

bool SerialWriteResult::accepted() const noexcept {
    return status == SerialWriteResultStatus::Accepted;
}

bool SerialWriteResult::rejected() const noexcept {
    return status == SerialWriteResultStatus::RejectedInvalid
        || status == SerialWriteResultStatus::RejectedFull;
}

bool SerialWriteResult::terminal() const noexcept {
    return status == SerialWriteResultStatus::Sent
        || status == SerialWriteResultStatus::Failed
        || status == SerialWriteResultStatus::Timeout
        || status == SerialWriteResultStatus::Cancelled;
}

bool SerialWriteQueueSnapshot::empty() const noexcept {
    return pendingCount == 0;
}

bool SerialWriteQueueSnapshot::full() const noexcept {
    return capacity > 0 && pendingCount >= capacity;
}

SerialWriteQueue::SerialWriteQueue(std::size_t capacity)
    : capacity_(capacity) {
}

std::size_t SerialWriteQueue::capacity() const noexcept {
    return capacity_;
}

std::size_t SerialWriteQueue::pendingCount() const noexcept {
    return pending_.size();
}

bool SerialWriteQueue::empty() const noexcept {
    return pending_.empty();
}

bool SerialWriteQueue::full() const noexcept {
    return capacity_ > 0 && pending_.size() >= capacity_;
}

SerialWriteQueueSnapshot SerialWriteQueue::snapshot() const noexcept {
    return {
        .capacity = capacity_,
        .pendingCount = pending_.size(),
        .nextRequestId = nextRequestId_,
    };
}

SerialWriteResult SerialWriteQueue::enqueue(std::vector<std::uint8_t> payload, int timeoutMs) {
    if (payload.empty()) {
        return reject(SerialWriteResultStatus::RejectedInvalid, kEmptyPayloadMessage);
    }
    if (timeoutMs <= 0) {
        return reject(SerialWriteResultStatus::RejectedInvalid, kInvalidTimeoutMessage);
    }
    if (full() || capacity_ == 0) {
        return reject(SerialWriteResultStatus::RejectedFull, kQueueFullMessage);
    }

    const SerialWriteRequestId requestId = allocateRequestId();
    SerialWriteRequest request;
    request.id = requestId;
    request.cancellationToken = {.requestId = requestId};
    request.payload = std::move(payload);
    request.timeoutMs = timeoutMs;
    pending_.push_back(std::move(request));
    return {
        .requestId = requestId,
        .status = SerialWriteResultStatus::Accepted,
        .byteCount = pending_.back().payload.size(),
    };
}

std::optional<SerialWriteRequest> SerialWriteQueue::peek() const {
    if (pending_.empty()) {
        return std::nullopt;
    }
    return pending_.front();
}

std::optional<SerialWriteRequest> SerialWriteQueue::takeNext() {
    if (pending_.empty()) {
        return std::nullopt;
    }
    SerialWriteRequest request = std::move(pending_.front());
    pending_.pop_front();
    return request;
}

SerialWriteResult SerialWriteQueue::cancelPending(SerialWriteRequestId requestId) {
    if (requestId == 0) {
        return reject(SerialWriteResultStatus::RejectedInvalid, kRequestNotFoundMessage);
    }
    const auto found = std::find_if(pending_.begin(), pending_.end(), [requestId](const SerialWriteRequest& request) {
        return request.id == requestId;
    });
    if (found == pending_.end()) {
        return reject(SerialWriteResultStatus::RejectedInvalid, kRequestNotFoundMessage);
    }

    found->cancellationState = SerialWriteCancellationState::Requested;
    const std::size_t byteCount = found->payload.size();
    pending_.erase(found);
    return {
        .requestId = requestId,
        .status = SerialWriteResultStatus::Cancelled,
        .byteCount = byteCount,
    };
}

SerialWriteResult SerialWriteQueue::cancelPending(SerialWriteCancellationToken token) {
    return cancelPending(token.requestId);
}

std::vector<SerialWriteResult> SerialWriteQueue::cancelAllPending() {
    std::vector<SerialWriteResult> results;
    results.reserve(pending_.size());
    while (!pending_.empty()) {
        SerialWriteRequest request = std::move(pending_.front());
        pending_.pop_front();
        request.cancellationState = SerialWriteCancellationState::Requested;
        results.push_back({
            .requestId = request.id,
            .status = SerialWriteResultStatus::Cancelled,
            .byteCount = request.payload.size(),
        });
    }
    return results;
}

SerialWriteResult SerialWriteQueue::completeNextSent(std::size_t byteCount) {
    if (pending_.empty()) {
        return reject(SerialWriteResultStatus::RejectedInvalid, kNoPendingRequestMessage);
    }
    if (byteCount != pending_.front().payload.size()) {
        return completeNext(SerialWriteResultStatus::Failed, byteCount, kPartialWriteMessage);
    }
    return completeNext(SerialWriteResultStatus::Sent, byteCount, {});
}

SerialWriteResult SerialWriteQueue::completeNextFailed(std::string message) {
    return completeNext(SerialWriteResultStatus::Failed, 0, std::move(message));
}

SerialWriteResult SerialWriteQueue::completeNextTimeout(std::string message) {
    return completeNext(SerialWriteResultStatus::Timeout, 0, std::move(message));
}

void SerialWriteQueue::clear() noexcept {
    pending_.clear();
}

SerialWriteRequestId SerialWriteQueue::allocateRequestId() noexcept {
    const SerialWriteRequestId requestId = nextRequestId_;
    ++nextRequestId_;
    if (nextRequestId_ == 0) {
        nextRequestId_ = 1;
    }
    return requestId;
}

SerialWriteResult SerialWriteQueue::reject(SerialWriteResultStatus status, std::string message) const {
    return {
        .status = status,
        .message = std::move(message),
    };
}

SerialWriteResult SerialWriteQueue::completeNext(SerialWriteResultStatus status, std::size_t byteCount, std::string message) {
    if (pending_.empty()) {
        return reject(SerialWriteResultStatus::RejectedInvalid, kNoPendingRequestMessage);
    }

    const SerialWriteRequest request = std::move(pending_.front());
    pending_.pop_front();
    return {
        .requestId = request.id,
        .status = status,
        .byteCount = byteCount,
        .message = std::move(message),
    };
}

const char* serialWriteResultStatusName(SerialWriteResultStatus status) noexcept {
    switch (status) {
    case SerialWriteResultStatus::Accepted:
        return "accepted";
    case SerialWriteResultStatus::RejectedInvalid:
        return "rejected-invalid";
    case SerialWriteResultStatus::RejectedFull:
        return "rejected-full";
    case SerialWriteResultStatus::Sent:
        return "sent";
    case SerialWriteResultStatus::Failed:
        return "failed";
    case SerialWriteResultStatus::Timeout:
        return "timeout";
    case SerialWriteResultStatus::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace svm::transport
