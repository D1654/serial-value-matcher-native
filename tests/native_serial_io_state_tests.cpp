#include "win32/native_serial_io_state.h"

#include <cassert>
#include <cstddef>
#include <iostream>

namespace {

using svm::win32::NativeSerialIoOwner;
using svm::win32::NativeSerialIoState;

void idleStateAllowsNewOwners() {
    NativeSerialIoState state;
    assert(state.isIdle());
    assert(!state.isBusy());
    assert(state.owner() == NativeSerialIoOwner::None);
    assert(state.allowsManualSend());
    assert(state.allowsFileSend());
    assert(state.allowsModbusScan());
    assert(state.allowsOwner(NativeSerialIoOwner::ManualSend));
    assert(state.allowsOwner(NativeSerialIoOwner::FileSend));
    assert(state.allowsOwner(NativeSerialIoOwner::ModbusScan));
    assert(!state.allowsOwner(NativeSerialIoOwner::None));
    assert(state.allowsSerialPoll());
    assert(state.allowsLineControl());
    assert(!state.shouldDeferDisconnect());
}

void manualSendIsExclusiveAndShortLived() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::ManualSend));
    assert(state.isBusy());
    assert(state.isOwnedBy(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsOwner(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(!state.allowsSerialPoll());
    assert(!state.allowsLineControl());
    assert(!state.release(NativeSerialIoOwner::FileSend));
    assert(state.release(NativeSerialIoOwner::ManualSend));
    assert(state.isIdle());
}

void fileSendOwnsWritesButKeepsPollingAllowed() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::FileSend));
    assert(state.isOwnedBy(NativeSerialIoOwner::FileSend));
    assert(!state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(state.allowsSerialPoll());
    assert(!state.allowsLineControl());
    assert(!state.shouldDeferDisconnect());
    assert(state.release(NativeSerialIoOwner::FileSend));
}

void modbusScanOwnsThePortUntilFinished() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::ModbusScan));
    assert(state.isOwnedBy(NativeSerialIoOwner::ModbusScan));
    assert(!state.tryAcquire(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(!state.allowsSerialPoll());
    assert(!state.allowsLineControl());
    assert(state.shouldDeferDisconnect());
    state.forceRelease();
    assert(state.isIdle());
}

void writeQueueStatusTracksPendingCapacityAndBackpressure() {
    NativeSerialIoState state;
    assert(state.writeQueueStatus().empty());
    assert(!state.writeQueueStatus().full());
    assert(!state.hasPendingSerialWrites());
    assert(!state.serialWriteQueueHasBackpressure());

    state.updateWriteQueueStatus(1, 4);
    assert(state.writeQueueStatus().pendingCount == 1);
    assert(state.writeQueueStatus().capacity == 4);
    assert(state.hasPendingSerialWrites());
    assert(!state.serialWriteQueueHasBackpressure());

    state.updateWriteQueueStatus(4, 4);
    assert(state.serialWriteQueueHasBackpressure());

    svm::transport::SerialWriteQueueSnapshot snapshot;
    snapshot.capacity = 8;
    snapshot.pendingCount = 2;
    state.updateWriteQueueStatus(snapshot);
    assert(state.writeQueueStatus().capacity == 8);
    assert(state.writeQueueStatus().pendingCount == 2);
    assert(!state.serialWriteQueueHasBackpressure());
}

} // namespace

int main() {
    idleStateAllowsNewOwners();
    manualSendIsExclusiveAndShortLived();
    fileSendOwnsWritesButKeepsPollingAllowed();
    modbusScanOwnsThePortUntilFinished();
    writeQueueStatusTracksPendingCapacityAndBackpressure();

    std::cout << "native_serial_io_state_tests passed\n";
    return 0;
}
