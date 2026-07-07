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

struct NativeSerialWriteQueueStatus {
    std::size_t pendingCount = 0;
    std::size_t capacity = 0;

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
    void updateWriteQueueStatus(std::size_t pendingCount, std::size_t capacity) noexcept;
    void updateWriteQueueStatus(const svm::transport::SerialWriteQueueSnapshot& snapshot) noexcept;
    bool hasPendingSerialWrites() const noexcept;
    bool serialWriteQueueHasBackpressure() const noexcept;

private:
    NativeSerialIoOwner owner_ = NativeSerialIoOwner::None;
    NativeSerialWriteQueueStatus writeQueueStatus_;
};

} // namespace svm::win32
