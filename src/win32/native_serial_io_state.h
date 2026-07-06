#pragma once

namespace svm::win32 {

enum class NativeSerialIoOwner {
    None,
    ManualSend,
    FileSend,
    ModbusScan,
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

private:
    NativeSerialIoOwner owner_ = NativeSerialIoOwner::None;
};

} // namespace svm::win32
