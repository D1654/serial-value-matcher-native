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

} // namespace svm::transport
