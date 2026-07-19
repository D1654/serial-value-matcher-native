#include "transport/serial_write_queue.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace svm::transport {
namespace {

SerialDeadline deadlineFromTimeout(int timeoutMs) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point now = Clock::now();
    const Clock::duration timeout = std::chrono::duration_cast<Clock::duration>(
        std::chrono::milliseconds(timeoutMs));
    const Clock::duration remaining = Clock::time_point::max() - now;
    return {
        .expiresAt = timeout >= remaining
            ? Clock::time_point::max()
            : now + timeout,
    };
}

} // namespace

bool SerialWriteQueueLimits::valid() const noexcept {
    return requestCapacity > 0 && byteCapacity > 0;
}

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
    return isSerialWriteResultRejected(status);
}

bool SerialWriteResult::terminal() const noexcept {
    return isSerialWriteResultTerminal(status);
}

SerialWriteQueue::SerialWriteQueue(std::size_t capacity)
    : limits_({
          .requestCapacity = capacity,
          .byteCapacity = kDefaultSerialWriteQueueByteCapacity,
      }) {
}

SerialWriteQueue::SerialWriteQueue(SerialWriteQueueLimits limits)
    : limits_(limits) {
}

std::size_t SerialWriteQueue::capacity() const noexcept {
    return limits_.requestCapacity;
}

std::size_t SerialWriteQueue::byteCapacity() const noexcept {
    return limits_.byteCapacity;
}

std::size_t SerialWriteQueue::pendingCount() const noexcept {
    return pending_.size();
}

std::size_t SerialWriteQueue::activeCount() const noexcept {
    return active_.has_value() ? 1 : 0;
}

bool SerialWriteQueue::empty() const noexcept {
    return pending_.empty() && !active_.has_value();
}

bool SerialWriteQueue::full() const noexcept {
    return snapshot().full();
}

SerialWriteQueueSnapshot SerialWriteQueue::snapshot() const noexcept {
    return {
        .generation = generation_,
        .capacity = limits_.requestCapacity,
        .byteCapacity = limits_.byteCapacity,
        .pendingCount = pending_.size(),
        .activeCount = activeCount(),
        .activeRequestId = active_.has_value()
            ? active_->requestId
            : kUnassignedSerialOperationId,
        .pendingBytes = pendingBytes_,
        .activeBytes = active_.has_value() ? active_->payloadBytes : 0,
        .highWaterCount = highWaterCount_,
        .highWaterBytes = highWaterBytes_,
        .nextRequestId = nextRequestId_,
    };
}

bool SerialWriteQueue::beginGeneration(SerialSessionGeneration generation) noexcept {
    if (generation == kUnassignedSerialSessionGeneration || !empty()) {
        return false;
    }
    generation_ = generation;
    highWaterCount_ = 0;
    highWaterBytes_ = 0;
    return true;
}

SerialWriteResult SerialWriteQueue::enqueue(
    std::vector<std::uint8_t> payload,
    int timeoutMs,
    SerialSessionGeneration generation,
    SerialDeadline deadline) {
    if (payload.empty()) {
        return reject(SerialWriteResultStatus::RejectedInvalid, generation, deadline);
    }
    if (timeoutMs <= 0) {
        return reject(SerialWriteResultStatus::RejectedInvalid, generation, deadline);
    }
    if (!limits_.valid()) {
        return reject(SerialWriteResultStatus::RejectedInvalid, generation, deadline);
    }
    if (generation != generation_
        && (!empty() || generation_ != kUnassignedSerialSessionGeneration)) {
        return reject(
            SerialWriteResultStatus::RejectedInvalid,
            generation,
            deadline);
    }
    const SerialWriteQueueSnapshot current = snapshot();
    const std::size_t payloadBytes = payload.size();
    if (current.countedCount() >= limits_.requestCapacity
        || payloadBytes > limits_.byteCapacity
        || current.countedBytes() > limits_.byteCapacity - payloadBytes) {
        return reject(SerialWriteResultStatus::RejectedFull, generation, deadline);
    }

    const std::optional<SerialWriteRequestId> requestId = reserveRequestId();
    if (!requestId.has_value()) {
        return reject(
            SerialWriteResultStatus::RejectedInvalid,
            generation,
            deadline);
    }
    if (generation_ == kUnassignedSerialSessionGeneration
        && generation != kUnassignedSerialSessionGeneration) {
        generation_ = generation;
        highWaterCount_ = 0;
        highWaterBytes_ = 0;
    }
    if (!deadline.set()) {
        deadline = deadlineFromTimeout(timeoutMs);
    }

    SerialWriteRequest request;
    request.id = *requestId;
    request.generation = generation;
    request.cancellationToken = {
        .requestId = *requestId,
        .generation = generation,
    };
    request.payload = std::move(payload);
    request.payloadBytes = payloadBytes;
    request.timeoutMs = timeoutMs;
    request.deadline = deadline;
    pending_.push_back(std::move(request));
    pendingBytes_ += payloadBytes;
    highWaterCount_ = std::max(highWaterCount_, snapshot().countedCount());
    highWaterBytes_ = std::max(highWaterBytes_, snapshot().countedBytes());
    return {
        .requestId = *requestId,
        .generation = generation,
        .status = SerialWriteResultStatus::Accepted,
        .byteCount = payloadBytes,
        .deadline = deadline,
    };
}

std::optional<SerialWriteRequest> SerialWriteQueue::peek() const {
    if (pending_.empty()) {
        return std::nullopt;
    }
    return pending_.front();
}

std::optional<SerialWriteRequest> SerialWriteQueue::activateNext() {
    if (active_.has_value() || pending_.empty()) {
        return std::nullopt;
    }

    SerialWriteRequest request = std::move(pending_.front());
    pending_.pop_front();
    pendingBytes_ -= request.payloadBytes;
    active_ = ActiveReservation{
        .requestId = request.id,
        .generation = request.generation,
        .payloadBytes = request.payloadBytes,
        .deadline = request.deadline,
    };
    return request;
}

SerialWriteResult SerialWriteQueue::cancelPending(SerialWriteRequestId requestId) {
    if (requestId == 0) {
        return reject(SerialWriteResultStatus::RejectedInvalid);
    }
    const auto found = std::find_if(pending_.begin(), pending_.end(), [requestId](const SerialWriteRequest& request) {
        return request.id == requestId;
    });
    if (found == pending_.end()) {
        return reject(SerialWriteResultStatus::RejectedInvalid);
    }

    found->cancellationState = SerialWriteCancellationState::Requested;
    const SerialWriteResult result{
        .requestId = found->id,
        .generation = found->generation,
        .status = SerialWriteResultStatus::Cancelled,
        .byteCount = 0,
        .deadline = found->deadline,
    };
    pendingBytes_ -= found->payloadBytes;
    pending_.erase(found);
    return result;
}

SerialWriteResult SerialWriteQueue::cancelPending(SerialWriteCancellationToken token) {
    if (!token.valid()) {
        return reject(SerialWriteResultStatus::RejectedInvalid);
    }
    const auto found = std::find_if(pending_.begin(), pending_.end(), [token](const SerialWriteRequest& request) {
        return request.id == token.requestId && request.generation == token.generation;
    });
    if (found == pending_.end()) {
        return rejectCompletion(token.requestId, token.generation);
    }
    return cancelPending(found->id);
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
            .generation = request.generation,
            .status = SerialWriteResultStatus::Cancelled,
            .byteCount = 0,
            .deadline = request.deadline,
        });
    }
    pendingBytes_ = 0;
    return results;
}

SerialWriteResult SerialWriteQueue::completeActive(
    SerialWriteRequestId requestId,
    SerialSessionGeneration generation,
    SerialWriteResultStatus status,
    std::size_t byteCount) {
    if (!isSerialWriteResultTerminal(status)) {
        return rejectCompletion(requestId, generation);
    }
    if (!active_.has_value()) {
        return rejectCompletion(requestId, generation);
    }
    if (active_->requestId != requestId || active_->generation != generation) {
        return rejectCompletion(requestId, generation);
    }
    if (byteCount > active_->payloadBytes) {
        return rejectCompletion(requestId, generation);
    }

    const ActiveReservation completed = *active_;
    active_.reset();
    if (status == SerialWriteResultStatus::Sent && byteCount != completed.payloadBytes) {
        status = SerialWriteResultStatus::Failed;
    }
    return {
        .requestId = completed.requestId,
        .generation = completed.generation,
        .status = status,
        .byteCount = byteCount,
        .deadline = completed.deadline,
    };
}

std::optional<SerialWriteRequestId> SerialWriteQueue::reserveRequestId() noexcept {
    if (nextRequestId_ == 0) {
        return std::nullopt;
    }
    const SerialWriteRequestId requestId = nextRequestId_;
    if (nextRequestId_ == std::numeric_limits<SerialWriteRequestId>::max()) {
        nextRequestId_ = 0;
    } else {
        ++nextRequestId_;
    }
    return requestId;
}

SerialWriteResult SerialWriteQueue::reject(
    SerialWriteResultStatus status,
    SerialSessionGeneration generation,
    SerialDeadline deadline) const {
    return {
        .generation = generation,
        .status = status,
        .deadline = deadline,
    };
}

SerialWriteResult SerialWriteQueue::rejectCompletion(
    SerialWriteRequestId requestId,
    SerialSessionGeneration generation) const {
    return {
        .requestId = requestId,
        .generation = generation,
        .status = SerialWriteResultStatus::RejectedInvalid,
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
    case SerialWriteResultStatus::Disconnected:
        return "disconnected";
    case SerialWriteResultStatus::Closed:
        return "closed";
    }
    return "unknown";
}

bool isSerialWriteResultRejected(SerialWriteResultStatus status) noexcept {
    return status == SerialWriteResultStatus::RejectedInvalid
        || status == SerialWriteResultStatus::RejectedFull;
}

bool isSerialWriteResultTerminal(SerialWriteResultStatus status) noexcept {
    return status == SerialWriteResultStatus::Sent
        || status == SerialWriteResultStatus::Failed
        || status == SerialWriteResultStatus::Timeout
        || status == SerialWriteResultStatus::Cancelled
        || status == SerialWriteResultStatus::Disconnected
        || status == SerialWriteResultStatus::Closed;
}

} // namespace svm::transport
