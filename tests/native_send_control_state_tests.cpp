#include "win32/native_send_control_state.h"

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

} // namespace

int main() {
    timedSendRequiresAllRuntimeConditions();
    timedSendPeriodIsAlwaysNormalized();
    quickSendValidationIsPureState();

    std::cout << "native_send_control_state_tests passed\n";
    return 0;
}
