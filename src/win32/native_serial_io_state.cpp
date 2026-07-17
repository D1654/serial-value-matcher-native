#include "win32/native_serial_io_state.h"

#include <utility>

namespace svm::win32 {

bool NativeSerialWriteKey::assigned() const noexcept {
    return generation != svm::transport::kUnassignedSerialSessionGeneration
        && requestId != svm::transport::kUnassignedSerialOperationId;
}

bool NativeSerialWriteKey::matches(
    const svm::transport::SerialOperationDescriptor& operation) const noexcept {
    return assigned()
        && operation.assigned()
        && generation == operation.generation
        && requestId == operation.requestId;
}

NativeSerialWriteKey nativeSerialWriteKeyForAdmission(
    const svm::transport::SerialWriteAdmissionResult& result,
    svm::transport::SerialSessionGeneration expectedGeneration) noexcept {
    if (!result.accepted()
        || result.operation.kind != svm::transport::SerialOperationKind::Write
        || !result.operation.assigned()
        || result.operation.generation == svm::transport::kUnassignedSerialSessionGeneration
        || result.operation.generation != expectedGeneration) {
        return {};
    }
    return {
        .generation = result.operation.generation,
        .requestId = result.operation.requestId,
    };
}

bool nativeSerialWriteCancellationMatches(
    const NativeSerialWriteKey& pending,
    const svm::transport::SerialTerminalResult& result,
    svm::transport::SerialSessionGeneration expectedGeneration) noexcept {
    return expectedGeneration != svm::transport::kUnassignedSerialSessionGeneration
        && pending.generation == expectedGeneration
        && result.status == svm::transport::SerialOperationStatus::Cancelled
        && result.error.category == svm::transport::SerialErrorCategory::Cancelled
        && pending.matches(result.operation);
}

NativeSerialWriteCompletionDecision nativeSerialWriteCompletionDecision(
    const NativeSerialWriteKey& pending,
    const svm::transport::SerialTerminalResult& result,
    const svm::transport::SerialSessionSnapshot& currentSession,
    svm::transport::SerialSessionGeneration faultGeneration) noexcept {
    if (!result.terminal()
        || result.operation.kind != svm::transport::SerialOperationKind::Write
        || !pending.matches(result.operation)) {
        return NativeSerialWriteCompletionDecision::Ignore;
    }

    const bool currentGeneration = currentSession.open()
        && currentSession.generation == result.operation.generation;
    if (!currentGeneration) {
        const bool currentFault = currentSession.state == svm::transport::SerialSessionState::Faulted
            && faultGeneration != svm::transport::kUnassignedSerialSessionGeneration
            && result.operation.generation == faultGeneration
            && result.status == svm::transport::SerialOperationStatus::Disconnected
            && result.error.category == svm::transport::SerialErrorCategory::Disconnected;
        if (!currentFault) {
            return NativeSerialWriteCompletionDecision::Ignore;
        }
    }

    switch (result.status) {
    case svm::transport::SerialOperationStatus::Succeeded:
        return currentGeneration
            ? NativeSerialWriteCompletionDecision::Succeeded
            : NativeSerialWriteCompletionDecision::Ignore;
    case svm::transport::SerialOperationStatus::Cancelled:
        return NativeSerialWriteCompletionDecision::Cancelled;
    case svm::transport::SerialOperationStatus::Failed:
    case svm::transport::SerialOperationStatus::Timeout:
    case svm::transport::SerialOperationStatus::Disconnected:
        return NativeSerialWriteCompletionDecision::Failed;
    case svm::transport::SerialOperationStatus::Accepted:
    case svm::transport::SerialOperationStatus::RejectedInvalid:
    case svm::transport::SerialOperationStatus::RejectedFull:
    case svm::transport::SerialOperationStatus::RejectedClosed:
        break;
    }
    return NativeSerialWriteCompletionDecision::Ignore;
}

NativeSerialReadDecision nativeSerialReadDecision(
    const svm::transport::SerialReadResult& result,
    svm::transport::SerialSessionGeneration expectedGeneration) noexcept {
    if (result.operation.operation.kind != svm::transport::SerialOperationKind::Read) {
        return NativeSerialReadDecision::ReportError;
    }
    if (expectedGeneration == svm::transport::kUnassignedSerialSessionGeneration
        || result.operation.operation.generation != expectedGeneration) {
        return NativeSerialReadDecision::Stop;
    }
    switch (result.operation.status) {
    case svm::transport::SerialOperationStatus::Succeeded:
        if (!result.operation.operation.assigned()) {
            return NativeSerialReadDecision::ReportError;
        }
        return result.bytes.empty()
            ? NativeSerialReadDecision::Stop
            : NativeSerialReadDecision::Append;
    case svm::transport::SerialOperationStatus::Timeout:
    case svm::transport::SerialOperationStatus::Cancelled:
    case svm::transport::SerialOperationStatus::RejectedClosed:
        return NativeSerialReadDecision::Stop;
    case svm::transport::SerialOperationStatus::Disconnected:
    case svm::transport::SerialOperationStatus::Failed:
        return NativeSerialReadDecision::Fail;
    case svm::transport::SerialOperationStatus::Accepted:
    case svm::transport::SerialOperationStatus::RejectedInvalid:
    case svm::transport::SerialOperationStatus::RejectedFull:
        return NativeSerialReadDecision::ReportError;
    }
    return NativeSerialReadDecision::ReportError;
}

NativeSerialIoOwner NativeSerialIoState::owner() const noexcept {
    return owner_;
}

bool NativeSerialIoState::isIdle() const noexcept {
    return owner_ == NativeSerialIoOwner::None;
}

bool NativeSerialIoState::isBusy() const noexcept {
    return !isIdle();
}

bool NativeSerialIoState::isOwnedBy(NativeSerialIoOwner owner) const noexcept {
    return owner_ == owner;
}

bool NativeSerialIoState::canAcquire(NativeSerialIoOwner owner) const noexcept {
    return allowsOwner(owner);
}

bool NativeSerialIoState::tryAcquire(NativeSerialIoOwner owner) noexcept {
    if (!canAcquire(owner)) {
        return false;
    }
    owner_ = owner;
    return true;
}

bool NativeSerialIoState::release(NativeSerialIoOwner owner) noexcept {
    if (owner_ != owner) {
        return false;
    }
    owner_ = NativeSerialIoOwner::None;
    return true;
}

void NativeSerialIoState::forceRelease() noexcept {
    owner_ = NativeSerialIoOwner::None;
}

bool NativeSerialIoState::allowsManualSend() const noexcept {
    return isIdle() && !serialWriteQueueHasBackpressure();
}

bool NativeSerialIoState::allowsFileSend() const noexcept {
    return isIdle() && !hasPendingSerialWrites();
}

bool NativeSerialIoState::allowsModbusScan() const noexcept {
    return isIdle() && !hasPendingSerialWrites();
}

bool NativeSerialIoState::allowsOwner(NativeSerialIoOwner owner) const noexcept {
    switch (owner) {
    case NativeSerialIoOwner::ManualSend:
        return allowsManualSend();
    case NativeSerialIoOwner::FileSend:
        return allowsFileSend();
    case NativeSerialIoOwner::ModbusScan:
        return allowsModbusScan();
    case NativeSerialIoOwner::None:
        break;
    }
    return false;
}

bool NativeSerialIoState::allowsSerialPoll() const noexcept {
    return owner_ != NativeSerialIoOwner::ModbusScan
        && owner_ != NativeSerialIoOwner::ManualSend;
}

bool NativeSerialIoState::allowsLineControl() const noexcept {
    return isIdle() && !hasPendingSerialWrites();
}

bool NativeSerialIoState::shouldDeferDisconnect() const noexcept {
    return owner_ == NativeSerialIoOwner::ModbusScan;
}

const std::optional<svm::transport::SerialWriteQueueSnapshot>&
NativeSerialIoState::writeQueueSnapshot() const noexcept {
    return writeQueueSnapshot_;
}

void NativeSerialIoState::updateWriteQueueSnapshot(
    svm::transport::SerialWriteQueueSnapshot snapshot) noexcept {
    writeQueueSnapshot_ = std::move(snapshot);
}

bool NativeSerialIoState::hasPendingSerialWrites() const noexcept {
    return writeQueueSnapshot_.has_value() && !writeQueueSnapshot_->empty();
}

bool NativeSerialIoState::serialWriteQueueHasBackpressure() const noexcept {
    return writeQueueSnapshot_.has_value() && writeQueueSnapshot_->full();
}

} // namespace svm::win32
