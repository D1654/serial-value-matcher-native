#pragma once

#include "transport/serial_write_queue.h"

#include <optional>

namespace svm::win32 {

enum class NativeSerialIoOwner {
    None,
    ManualSend,
    FileSend,
    ModbusScan,
};

struct NativeSerialWriteKey {
    svm::transport::SerialSessionGeneration generation =
        svm::transport::kUnassignedSerialSessionGeneration;
    svm::transport::SerialOperationId requestId =
        svm::transport::kUnassignedSerialOperationId;

    bool assigned() const noexcept;
    bool matches(const svm::transport::SerialOperationDescriptor& operation) const noexcept;
};

NativeSerialWriteKey nativeSerialWriteKeyForAdmission(
    const svm::transport::SerialWriteAdmissionResult& result,
    svm::transport::SerialSessionGeneration expectedGeneration) noexcept;

bool nativeSerialWriteCancellationMatches(
    const NativeSerialWriteKey& pending,
    const svm::transport::SerialTerminalResult& result,
    svm::transport::SerialSessionGeneration expectedGeneration) noexcept;

enum class NativeSerialWriteCompletionDecision {
    Ignore,
    Succeeded,
    Cancelled,
    Failed,
};

NativeSerialWriteCompletionDecision nativeSerialWriteCompletionDecision(
    const NativeSerialWriteKey& pending,
    const svm::transport::SerialTerminalResult& result,
    const svm::transport::SerialSessionSnapshot& currentSession,
    svm::transport::SerialSessionGeneration faultGeneration) noexcept;

enum class NativeSerialReadDecision {
    Append,
    Stop,
    Fail,
    ReportError,
};

NativeSerialReadDecision nativeSerialReadDecision(
    const svm::transport::SerialReadResult& result,
    svm::transport::SerialSessionGeneration expectedGeneration) noexcept;

class NativeSerialIoState final {
public:
    NativeSerialIoOwner owner() const noexcept;
    bool isIdle() const noexcept;
    bool isBusy() const noexcept;
    bool isOwnedBy(NativeSerialIoOwner owner) const noexcept;

    bool canAcquire(NativeSerialIoOwner owner) const noexcept;
    bool tryAcquire(NativeSerialIoOwner owner) noexcept;
    bool release(NativeSerialIoOwner owner) noexcept;
    void forceRelease() noexcept;

    bool allowsManualSend() const noexcept;
    bool allowsFileSend() const noexcept;
    bool allowsModbusScan() const noexcept;
    bool allowsOwner(NativeSerialIoOwner owner) const noexcept;
    bool allowsSerialPoll() const noexcept;
    bool allowsLineControl() const noexcept;
    bool shouldDeferDisconnect() const noexcept;

    const std::optional<svm::transport::SerialWriteQueueSnapshot>& writeQueueSnapshot() const noexcept;
    void updateWriteQueueSnapshot(svm::transport::SerialWriteQueueSnapshot snapshot) noexcept;
    bool hasPendingSerialWrites() const noexcept;
    bool serialWriteQueueHasBackpressure() const noexcept;

private:
    NativeSerialIoOwner owner_ = NativeSerialIoOwner::None;
    std::optional<svm::transport::SerialWriteQueueSnapshot> writeQueueSnapshot_;
};

} // namespace svm::win32
