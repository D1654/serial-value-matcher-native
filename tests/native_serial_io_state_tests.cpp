#include "win32/native_serial_io_state.h"

#include <cassert>
#include <iostream>

namespace {

using svm::win32::NativeSerialIoOwner;
using svm::win32::NativeSerialIoState;

void idleStateAllowsNewOwners() {
    NativeSerialIoState state;
    assert(state.isIdle());
    assert(state.owner() == NativeSerialIoOwner::None);
    assert(state.allowsManualSend());
    assert(state.allowsFileSend());
    assert(state.allowsModbusScan());
    assert(state.allowsSerialPoll());
    assert(state.allowsLineControl());
    assert(!state.shouldDeferDisconnect());
}

void manualSendIsExclusiveAndShortLived() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::ManualSend));
    assert(state.isOwnedBy(NativeSerialIoOwner::ManualSend));
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

} // namespace

int main() {
    idleStateAllowsNewOwners();
    manualSendIsExclusiveAndShortLived();
    fileSendOwnsWritesButKeepsPollingAllowed();
    modbusScanOwnsThePortUntilFinished();

    std::cout << "native_serial_io_state_tests passed\n";
    return 0;
}
