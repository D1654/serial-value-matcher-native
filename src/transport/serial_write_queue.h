#pragma once

#include "transport/serial_types.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace svm::transport {

using SerialWriteRequestId = SerialOperationId;

inline constexpr std::size_t kDefaultSerialWriteQueueCapacity = 64;
inline constexpr std::size_t kDefaultSerialWriteQueueByteCapacity = 256 * 1024;
inline constexpr int kDefaultSerialWriteTimeoutMs = kSerialTerminalResultTargetMs;

struct SerialWriteQueueLimits {
    std::size_t requestCapacity = kDefaultSerialWriteQueueCapacity;
    std::size_t byteCapacity = kDefaultSerialWriteQueueByteCapacity;

    bool valid() const noexcept;
};

enum class SerialWriteCancellationState {
    Active,
    Requested,
};

enum class SerialWriteResultStatus {
    Accepted,
    RejectedInvalid,
    RejectedFull,
    Sent,
    Failed,
    Timeout,
    Cancelled,
    Disconnected,
    Closed,
};

struct SerialWriteCancellationToken {
    SerialWriteRequestId requestId = 0;
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;

    bool valid() const noexcept;
};

struct SerialWriteRequest {
    SerialWriteRequestId id = 0;
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
    SerialWriteCancellationToken cancellationToken;
    std::vector<std::uint8_t> payload;
    std::size_t payloadBytes = 0;
    int timeoutMs = kDefaultSerialWriteTimeoutMs;
    SerialDeadline deadline;
    SerialWriteCancellationState cancellationState = SerialWriteCancellationState::Active;

    bool cancellationRequested() const noexcept;
};

struct SerialWriteResult {
    SerialWriteRequestId requestId = 0;
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
    SerialWriteResultStatus status = SerialWriteResultStatus::RejectedInvalid;
    std::size_t byteCount = 0;
    SerialDeadline deadline;
    std::string message;

    bool accepted() const noexcept;
    bool rejected() const noexcept;
    bool terminal() const noexcept;
};

struct SerialWriteQueueSnapshot {
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
    std::size_t capacity = 0;
    std::size_t byteCapacity = 0;
    std::size_t pendingCount = 0;
    std::size_t activeCount = 0;
    SerialWriteRequestId activeRequestId = kUnassignedSerialOperationId;
    std::size_t pendingBytes = 0;
    std::size_t activeBytes = 0;
    std::size_t highWaterCount = 0;
    std::size_t highWaterBytes = 0;
    SerialWriteRequestId nextRequestId = 1;

    std::size_t countedCount() const noexcept {
        return pendingCount + activeCount;
    }

    std::size_t countedBytes() const noexcept {
        return pendingBytes + activeBytes;
    }

    bool empty() const noexcept {
        return countedCount() == 0;
    }

    bool full() const noexcept {
        return capacity == 0
            || byteCapacity == 0
            || countedCount() >= capacity
            || countedBytes() >= byteCapacity;
    }
};

class SerialWriteQueue final {
public:
    explicit SerialWriteQueue(std::size_t capacity = kDefaultSerialWriteQueueCapacity);
    explicit SerialWriteQueue(SerialWriteQueueLimits limits);

    std::size_t capacity() const noexcept;
    std::size_t byteCapacity() const noexcept;
    std::size_t pendingCount() const noexcept;
    std::size_t activeCount() const noexcept;
    bool empty() const noexcept;
    bool full() const noexcept;
    SerialWriteQueueSnapshot snapshot() const noexcept;
    bool beginGeneration(SerialSessionGeneration generation) noexcept;
    std::optional<SerialWriteRequestId> reserveRequestId() noexcept;

    SerialWriteResult enqueue(
        std::vector<std::uint8_t> payload,
        int timeoutMs = kDefaultSerialWriteTimeoutMs,
        SerialSessionGeneration generation = kUnassignedSerialSessionGeneration,
        SerialDeadline deadline = {});
    std::optional<SerialWriteRequest> peek() const;
    std::optional<SerialWriteRequest> activateNext();

    SerialWriteResult cancelPending(SerialWriteRequestId requestId);
    SerialWriteResult cancelPending(SerialWriteCancellationToken token);
    std::vector<SerialWriteResult> cancelAllPending();
    SerialWriteResult completeActive(
        SerialWriteRequestId requestId,
        SerialSessionGeneration generation,
        SerialWriteResultStatus status,
        std::size_t byteCount = 0,
        std::string message = {});

private:
    struct ActiveReservation {
        SerialWriteRequestId requestId = 0;
        SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
        std::size_t payloadBytes = 0;
        SerialDeadline deadline;
    };

    SerialWriteResult reject(
        SerialWriteResultStatus status,
        std::string message,
        SerialSessionGeneration generation = kUnassignedSerialSessionGeneration,
        SerialDeadline deadline = {}) const;
    SerialWriteResult rejectCompletion(
        SerialWriteRequestId requestId,
        SerialSessionGeneration generation,
        std::string message) const;

    SerialWriteQueueLimits limits_;
    SerialSessionGeneration generation_ = kUnassignedSerialSessionGeneration;
    SerialWriteRequestId nextRequestId_ = 1;
    std::size_t pendingBytes_ = 0;
    std::size_t highWaterCount_ = 0;
    std::size_t highWaterBytes_ = 0;
    std::deque<SerialWriteRequest> pending_;
    std::optional<ActiveReservation> active_;
};

const char* serialWriteResultStatusName(SerialWriteResultStatus status) noexcept;
bool isSerialWriteResultRejected(SerialWriteResultStatus status) noexcept;
bool isSerialWriteResultTerminal(SerialWriteResultStatus status) noexcept;

} // namespace svm::transport
