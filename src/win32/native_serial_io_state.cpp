#include "win32/native_serial_io_state.h"

namespace svm::win32 {

NativeSerialIoOwner NativeSerialIoState::owner() const noexcept {
    return owner_;
}

bool NativeSerialIoState::isIdle() const noexcept {
    return owner_ == NativeSerialIoOwner::None;
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

} // namespace svm::win32
