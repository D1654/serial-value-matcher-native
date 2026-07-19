#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace svm::transport {

enum class SerialParity {
    None,
    Odd,
    Even,
    Mark,
    Space,
};

enum class SerialStopBits {
    One,
    OnePointFive,
    Two,
};

enum class SerialFlowControl {
    None,
    HardwareRtsCts,
    SoftwareXonXoff,
};

inline constexpr int kSerialTerminalResultTargetMs = 1000;

struct SerialOpenOptions {
    std::string portName;
    int baudRate = 115200;
    int dataBits = 8;
    SerialParity parity = SerialParity::None;
    SerialStopBits stopBits = SerialStopBits::One;
    SerialFlowControl flowControl = SerialFlowControl::None;
    bool dataTerminalReady = false;
    bool requestToSend = false;
    int readTimeoutMs = 1000;
    int writeTimeoutMs = kSerialTerminalResultTargetMs;
    std::size_t readBufferSize = 4096;
};

using SerialOperationId = std::uint64_t;
using SerialSessionGeneration = std::uint64_t;

inline constexpr SerialOperationId kUnassignedSerialOperationId = 0;
inline constexpr SerialSessionGeneration kUnassignedSerialSessionGeneration = 0;

enum class SerialSessionState {
    Closed,
    Opening,
    Open,
    Closing,
    Faulted,
};

enum class SerialOperationKind {
    Open,
    Close,
    Read,
    Write,
    SetDataTerminalReady,
    SetRequestToSend,
};

enum class SerialDataDirection {
    None,
    Transmit,
    Receive,
};

enum class SerialOperationStatus {
    Accepted,
    Succeeded,
    RejectedInvalid,
    RejectedFull,
    RejectedClosed,
    Failed,
    Timeout,
    Cancelled,
    Disconnected,
};

enum class SerialDeadlineStatus {
    NotSet,
    Pending,
    Met,
    Expired,
};

enum class SerialErrorCategory {
    None,
    InvalidInput,
    SessionClosed,
    QueueFull,
    Timeout,
    Cancelled,
    Disconnected,
    NativeFailure,
    IoFailure,
};

struct SerialDeadline {
    std::optional<std::chrono::steady_clock::time_point> expiresAt;

    [[nodiscard]] bool set() const noexcept {
        return expiresAt.has_value();
    }
};

struct SerialErrorEvidence {
    SerialErrorCategory category = SerialErrorCategory::None;
    std::uint32_t nativeCode = 0;
    std::size_t byteCount = 0;
    std::uint32_t commErrorMask = 0;
    std::optional<std::size_t> inputQueueBytes;
    std::optional<std::size_t> outputQueueBytes;

    [[nodiscard]] bool ok() const noexcept {
        return category == SerialErrorCategory::None;
    }
};

struct SerialOperationDescriptor {
    SerialOperationId requestId = kUnassignedSerialOperationId;
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
    SerialOperationKind kind = SerialOperationKind::Read;
    SerialDeadline deadline;

    [[nodiscard]] bool assigned() const noexcept {
        return requestId != kUnassignedSerialOperationId;
    }
};

struct SerialOperationResult {
    SerialOperationDescriptor operation;
    SerialOperationStatus status = SerialOperationStatus::Failed;
    SerialDeadlineStatus deadlineStatus = SerialDeadlineStatus::NotSet;
    std::size_t byteCount = 0;
    std::string endpoint;
    SerialErrorEvidence error;

    [[nodiscard]] bool accepted() const noexcept {
        return status == SerialOperationStatus::Accepted;
    }

    [[nodiscard]] bool succeeded() const noexcept {
        return status == SerialOperationStatus::Succeeded;
    }

    [[nodiscard]] bool rejected() const noexcept {
        return status == SerialOperationStatus::RejectedInvalid
            || status == SerialOperationStatus::RejectedFull
            || status == SerialOperationStatus::RejectedClosed;
    }

    [[nodiscard]] bool terminal() const noexcept {
        return status == SerialOperationStatus::Succeeded
            || status == SerialOperationStatus::Failed
            || status == SerialOperationStatus::Timeout
            || status == SerialOperationStatus::Cancelled
            || status == SerialOperationStatus::Disconnected;
    }
};

using SerialWriteAdmissionResult = SerialOperationResult;
using SerialTerminalResult = SerialOperationResult;

struct SerialReadResult {
    SerialOperationResult operation;
    std::vector<std::uint8_t> bytes;
};

struct SerialSessionSnapshot {
    SerialSessionState state = SerialSessionState::Closed;
    SerialSessionGeneration generation = kUnassignedSerialSessionGeneration;
    std::string endpoint;
    SerialOpenOptions options;

    [[nodiscard]] bool open() const noexcept {
        return state == SerialSessionState::Open;
    }

    [[nodiscard]] bool usesHardwareRtsCts() const noexcept {
        return options.flowControl == SerialFlowControl::HardwareRtsCts;
    }
};

constexpr const char* serialSessionStateName(SerialSessionState state) noexcept {
    switch (state) {
    case SerialSessionState::Closed:
        return "closed";
    case SerialSessionState::Opening:
        return "opening";
    case SerialSessionState::Open:
        return "open";
    case SerialSessionState::Closing:
        return "closing";
    case SerialSessionState::Faulted:
        return "faulted";
    }
    return "unknown";
}

constexpr SerialDataDirection serialOperationDirection(SerialOperationKind kind) noexcept {
    switch (kind) {
    case SerialOperationKind::Write:
        return SerialDataDirection::Transmit;
    case SerialOperationKind::Read:
        return SerialDataDirection::Receive;
    case SerialOperationKind::Open:
    case SerialOperationKind::Close:
    case SerialOperationKind::SetDataTerminalReady:
    case SerialOperationKind::SetRequestToSend:
        return SerialDataDirection::None;
    }
    return SerialDataDirection::None;
}

constexpr const char* serialDataDirectionName(SerialDataDirection direction) noexcept {
    switch (direction) {
    case SerialDataDirection::None:
        return "none";
    case SerialDataDirection::Transmit:
        return "tx";
    case SerialDataDirection::Receive:
        return "rx";
    }
    return "unknown";
}

constexpr const char* serialOperationKindName(SerialOperationKind kind) noexcept {
    switch (kind) {
    case SerialOperationKind::Open:
        return "open";
    case SerialOperationKind::Close:
        return "close";
    case SerialOperationKind::Read:
        return "read";
    case SerialOperationKind::Write:
        return "write";
    case SerialOperationKind::SetDataTerminalReady:
        return "set_dtr";
    case SerialOperationKind::SetRequestToSend:
        return "set_rts";
    }
    return "unknown";
}

constexpr const char* serialOperationStatusName(SerialOperationStatus status) noexcept {
    switch (status) {
    case SerialOperationStatus::Accepted:
        return "accepted";
    case SerialOperationStatus::Succeeded:
        return "succeeded";
    case SerialOperationStatus::RejectedInvalid:
        return "rejected_invalid";
    case SerialOperationStatus::RejectedFull:
        return "rejected_full";
    case SerialOperationStatus::RejectedClosed:
        return "rejected_closed";
    case SerialOperationStatus::Failed:
        return "failed";
    case SerialOperationStatus::Timeout:
        return "timeout";
    case SerialOperationStatus::Cancelled:
        return "cancelled";
    case SerialOperationStatus::Disconnected:
        return "disconnected";
    }
    return "unknown";
}

constexpr const char* serialDeadlineStatusName(SerialDeadlineStatus status) noexcept {
    switch (status) {
    case SerialDeadlineStatus::NotSet:
        return "not_set";
    case SerialDeadlineStatus::Pending:
        return "pending";
    case SerialDeadlineStatus::Met:
        return "met";
    case SerialDeadlineStatus::Expired:
        return "expired";
    }
    return "unknown";
}

constexpr const char* serialErrorCategoryName(SerialErrorCategory category) noexcept {
    switch (category) {
    case SerialErrorCategory::None:
        return "none";
    case SerialErrorCategory::InvalidInput:
        return "invalid_input";
    case SerialErrorCategory::SessionClosed:
        return "session_closed";
    case SerialErrorCategory::QueueFull:
        return "queue_full";
    case SerialErrorCategory::Timeout:
        return "timeout";
    case SerialErrorCategory::Cancelled:
        return "cancelled";
    case SerialErrorCategory::Disconnected:
        return "disconnected";
    case SerialErrorCategory::NativeFailure:
        return "native_failure";
    case SerialErrorCategory::IoFailure:
        return "io_failure";
    }
    return "unknown";
}

} // namespace svm::transport
