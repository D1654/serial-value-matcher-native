#include "win32/native_serial_io_state.h"

namespace svm::win32 {

bool NativeSerialWriteQueueStatus::empty() const noexcept {
    return pendingCount == 0;
}

bool NativeSerialWriteQueueStatus::full() const noexcept {
    return capacity > 0 && pendingCount >= capacity;
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
    return owner != NativeSerialIoOwner::None && isIdle();
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
    return isIdle();
}

bool NativeSerialIoState::allowsFileSend() const noexcept {
    return isIdle();
}

bool NativeSerialIoState::allowsModbusScan() const noexcept {
    return isIdle();
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
    return isIdle();
}

bool NativeSerialIoState::shouldDeferDisconnect() const noexcept {
    return owner_ == NativeSerialIoOwner::ModbusScan;
}

NativeSerialWriteQueueStatus NativeSerialIoState::writeQueueStatus() const noexcept {
    return writeQueueStatus_;
}

void NativeSerialIoState::updateWriteQueueStatus(std::size_t pendingCount, std::size_t capacity) noexcept {
    writeQueueStatus_ = {
        .pendingCount = pendingCount,
        .capacity = capacity,
    };
}

void NativeSerialIoState::updateWriteQueueStatus(const svm::transport::SerialWriteQueueSnapshot& snapshot) noexcept {
    updateWriteQueueStatus(snapshot.pendingCount, snapshot.capacity);
}

bool NativeSerialIoState::hasPendingSerialWrites() const noexcept {
    return !writeQueueStatus_.empty();
}

bool NativeSerialIoState::serialWriteQueueHasBackpressure() const noexcept {
    return writeQueueStatus_.full();
}

} // namespace svm::win32
