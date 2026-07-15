#pragma once

#include "transport/serial_write_queue.h"

#include <cstddef>

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

struct NativeSerialWriteQueueStatus {
    svm::transport::SerialSessionGeneration generation =
        svm::transport::kUnassignedSerialSessionGeneration;
    std::size_t pendingCount = 0;
    std::size_t activeCount = 0;
    std::size_t requestCapacity = 0;
    std::size_t pendingBytes = 0;
    std::size_t activeBytes = 0;
    std::size_t byteCapacity = 0;

    std::size_t countedCount() const noexcept;
    std::size_t countedBytes() const noexcept;
    bool empty() const noexcept;
    bool full() const noexcept;
};

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

    NativeSerialWriteQueueStatus writeQueueStatus() const noexcept;
    void updateWriteQueueStatus(
        svm::transport::SerialSessionGeneration generation,
        const svm::transport::SerialWriteQueueSnapshot& snapshot) noexcept;
    bool hasPendingSerialWrites() const noexcept;
    bool serialWriteQueueHasBackpressure() const noexcept;

private:
    NativeSerialIoOwner owner_ = NativeSerialIoOwner::None;
    NativeSerialWriteQueueStatus writeQueueStatus_;
};

} // namespace svm::win32
