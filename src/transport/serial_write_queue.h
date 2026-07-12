#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace svm::transport {

using SerialWriteRequestId = std::uint64_t;

inline constexpr std::size_t kDefaultSerialWriteQueueCapacity = 64;
inline constexpr int kDefaultSerialWriteTimeoutMs = 1000;

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
};

struct SerialWriteCancellationToken {
    SerialWriteRequestId requestId = 0;

    bool valid() const noexcept;
};

struct SerialWriteRequest {
    SerialWriteRequestId id = 0;
    SerialWriteCancellationToken cancellationToken;
    std::vector<std::uint8_t> payload;
    int timeoutMs = kDefaultSerialWriteTimeoutMs;
    SerialWriteCancellationState cancellationState = SerialWriteCancellationState::Active;

    bool cancellationRequested() const noexcept;
};

struct SerialWriteResult {
    SerialWriteRequestId requestId = 0;
    SerialWriteResultStatus status = SerialWriteResultStatus::RejectedInvalid;
    std::size_t byteCount = 0;
    std::string message;

    bool accepted() const noexcept;
    bool rejected() const noexcept;
    bool terminal() const noexcept;
};

struct SerialWriteQueueSnapshot {
    std::size_t capacity = 0;
    std::size_t pendingCount = 0;
    SerialWriteRequestId nextRequestId = 1;

    bool empty() const noexcept;
    bool full() const noexcept;
};

class SerialWritePort {
public:
    virtual ~SerialWritePort() = default;

    virtual SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) = 0;
};

class SerialWriteQueue final : public SerialWritePort {
public:
    explicit SerialWriteQueue(std::size_t capacity = kDefaultSerialWriteQueueCapacity);

    std::size_t capacity() const noexcept;
    std::size_t pendingCount() const noexcept;
    bool empty() const noexcept;
    bool full() const noexcept;
    SerialWriteQueueSnapshot snapshot() const noexcept;

    SerialWriteResult enqueue(std::vector<std::uint8_t> payload, int timeoutMs = kDefaultSerialWriteTimeoutMs);
    SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) override;
    std::optional<SerialWriteRequest> peek() const;
    std::optional<SerialWriteRequest> takeNext();

    SerialWriteResult cancelPending(SerialWriteRequestId requestId);
    SerialWriteResult cancelPending(SerialWriteCancellationToken token);
    std::vector<SerialWriteResult> cancelAllPending();
    SerialWriteResult completeNextSent(std::size_t byteCount);
    SerialWriteResult completeNextFailed(std::string message);
    SerialWriteResult completeNextTimeout(std::string message = {});

    void clear() noexcept;

private:
    SerialWriteRequestId allocateRequestId() noexcept;
    SerialWriteResult reject(SerialWriteResultStatus status, std::string message) const;
    SerialWriteResult completeNext(SerialWriteResultStatus status, std::size_t byteCount, std::string message);

    std::size_t capacity_ = kDefaultSerialWriteQueueCapacity;
    SerialWriteRequestId nextRequestId_ = 1;
    std::deque<SerialWriteRequest> pending_;
};

const char* serialWriteResultStatusName(SerialWriteResultStatus status) noexcept;
bool isSerialWriteResultRejected(SerialWriteResultStatus status) noexcept;
bool isSerialWriteResultTerminal(SerialWriteResultStatus status) noexcept;

} // namespace svm::transport
