#include "win32/native_send_control_state.h"
#include "win32/native_serial_send_controller.h"

#include <cassert>
#include <iostream>

namespace {

void timedSendRequiresAllRuntimeConditions() {
    svm::win32::NativeSendControlState state;

    auto decision = state.timerDecision(true, true, 1000);
    assert(!decision.shouldRun);
    assert(decision.periodMs == 1000);

    state.setTimedSendEnabled(true);
    assert(state.timedSendEnabled());
    assert(state.canRunTimedSend(true, true));
    assert(!state.canRunTimedSend(false, true));
    assert(!state.canRunTimedSend(true, false));

    decision = state.timerDecision(false, true, 1000);
    assert(!decision.shouldRun);

    decision = state.timerDecision(true, false, 1000);
    assert(!decision.shouldRun);

    decision = state.timerDecision(true, true, 1000);
    assert(decision.shouldRun);
    assert(decision.periodMs == 1000);
}

void timedSendPeriodIsAlwaysNormalized() {
    svm::win32::NativeSendControlState state;
    state.setTimedSendEnabled(true);

    auto decision = state.timerDecision(true, true, 1);
    assert(decision.shouldRun);
    assert(decision.periodMs == svm::win32::kNativeMinTimedSendPeriodMs);

    decision = state.timerDecision(true, true, 4000000);
    assert(decision.shouldRun);
    assert(decision.periodMs == svm::win32::kNativeMaxTimedSendPeriodMs);
}

void quickSendValidationIsPureState() {
    svm::win32::NativeSendControlState state;
    assert(state.isQuickSendIndexValid(0, 10));
    assert(state.isQuickSendIndexValid(9, 10));
    assert(!state.isQuickSendIndexValid(10, 10));

    assert(!state.isQuickSendTextUsable(L""));
    assert(state.isQuickSendTextUsable(L"AT"));
}

void controllerKeepsManualSendDecisionsPure() {
    svm::win32::NativeSerialSendController controller;
    svm::win32::NativeSerialIoState ioState;

    auto decision = controller.manualSendAvailability(false, ioState);
    assert(!decision.allowed());
    assert(decision.kind == svm::win32::NativeSerialSendDecisionKind::SerialNotConnected);

    decision = controller.manualSendAvailability(true, ioState);
    assert(decision.allowed());
    assert(decision.owner == svm::win32::NativeSerialIoOwner::ManualSend);

    assert(ioState.tryAcquire(svm::win32::NativeSerialIoOwner::FileSend));
    decision = controller.manualSendAvailability(true, ioState);
    assert(!decision.allowed());
    assert(decision.kind == svm::win32::NativeSerialSendDecisionKind::SerialIoBusy);

    decision = controller.manualPayloadDecision(true, false);
    assert(decision.kind == svm::win32::NativeSerialSendDecisionKind::PayloadInvalid);
    decision = controller.manualPayloadDecision(false, true);
    assert(decision.kind == svm::win32::NativeSerialSendDecisionKind::PayloadEmpty);
    decision = controller.manualAcquireDecision(false);
    assert(decision.kind == svm::win32::NativeSerialSendDecisionKind::SerialIoBusy);
}

void controllerCoversQuickTimedAndFileDecisions() {
    svm::win32::NativeSerialSendController controller;
    svm::win32::NativeSendControlState sendState;
    svm::win32::NativeSerialIoState ioState;

    auto quick = controller.quickSendDecision(sendState, 10, 10, L"AT");
    assert(quick.ignored());
    quick = controller.quickSendDecision(sendState, 0, 10, L"");
    assert(quick.kind == svm::win32::NativeSerialSendDecisionKind::QuickSendEmpty);
    quick = controller.quickSendDecision(sendState, 0, 10, L"AT");
    assert(quick.allowed());
    assert(quick.owner == svm::win32::NativeSerialIoOwner::ManualSend);

    sendState.setTimedSendEnabled(true);
    auto timer = controller.timedSendDecision(sendState, true, ioState, 1);
    assert(timer.shouldRun);
    assert(timer.periodMs == svm::win32::kNativeMinTimedSendPeriodMs);

    auto file = controller.fileStartDecision(true, ioState, false, L"C:\\data.bin");
    assert(file.allowed());
    assert(file.owner == svm::win32::NativeSerialIoOwner::FileSend);
    file = controller.fileStartDecision(true, ioState, true, L"C:\\data.bin");
    assert(file.ignored());
    file = controller.fileStartDecision(true, ioState, false, L"");
    assert(file.kind == svm::win32::NativeSerialSendDecisionKind::FilePathEmpty);

    auto pump = controller.filePumpDecision(false, true, ioState);
    assert(pump.ignored());
    pump = controller.filePumpDecision(true, false, ioState);
    assert(pump.kind == svm::win32::NativeSerialSendDecisionKind::FileDisconnected);
    pump = controller.filePumpDecision(true, true, ioState);
    assert(pump.kind == svm::win32::NativeSerialSendDecisionKind::SerialIoBusy);
    assert(ioState.tryAcquire(svm::win32::NativeSerialIoOwner::FileSend));
    pump = controller.filePumpDecision(true, true, ioState);
    assert(pump.allowed());
}

} // namespace

int main() {
    timedSendRequiresAllRuntimeConditions();
    timedSendPeriodIsAlwaysNormalized();
    quickSendValidationIsPureState();
    controllerKeepsManualSendDecisionsPure();
    controllerCoversQuickTimedAndFileDecisions();

    std::cout << "native_send_control_state_tests passed\n";
    return 0;
}
