#include "win32/native_status_counters_state.h"

#include <cassert>
#include <iostream>

namespace {

void countersStartAtZero() {
    svm::win32::NativeStatusCountersState state;
    assert(state.txBytes() == 0);
    assert(state.rxBytes() == 0);
    assert(state.txStatusText() == L"TX 0 B");
    assert(state.rxStatusText() == L"RX 0 B");
}

void countersAccumulateIndependently() {
    svm::win32::NativeStatusCountersState state;
    state.addTxBytes(3);
    state.addTxBytes(7);
    state.addRxBytes(11);

    assert(state.txBytes() == 10);
    assert(state.rxBytes() == 11);
    assert(state.txStatusText() == L"TX 10 B");
    assert(state.rxStatusText() == L"RX 11 B");
}

void resetClearsBothCounters() {
    svm::win32::NativeStatusCountersState state;
    state.addTxBytes(1);
    state.addRxBytes(2);

    state.reset();
    assert(state.txBytes() == 0);
    assert(state.rxBytes() == 0);
}

} // namespace

int main() {
    countersStartAtZero();
    countersAccumulateIndependently();
    resetClearsBothCounters();

    std::cout << "native_status_counters_state_tests passed\n";
    return 0;
}
