#include "win32/native_connection_ui_state.h"

#include <cassert>
#include <iostream>

namespace {

void buttonModeTracksSerialOpenState() {
    svm::win32::NativeConnectionUiState state;
    assert(state.buttonMode(false) == svm::win32::NativeConnectionButtonMode::Connect);
    assert(state.buttonMode(true) == svm::win32::NativeConnectionButtonMode::Disconnect);
}

void rtsDisabledWhenLineControlBusy() {
    svm::win32::NativeConnectionUiState state;
    const auto ui = state.lineControlState(false, false, false, false);
    assert(!ui.rtsEnabled);
}

void rtsDisabledWhenHardwareFlowControlOwnsIt() {
    svm::win32::NativeConnectionUiState state;
    assert(!state.lineControlState(true, false, true, false).rtsEnabled);
    assert(!state.lineControlState(true, true, false, true).rtsEnabled);
}

void rtsEnabledWhenManualControlIsAvailable() {
    svm::win32::NativeConnectionUiState state;
    assert(state.lineControlState(true, false, false, false).rtsEnabled);
    assert(state.lineControlState(true, true, true, false).rtsEnabled);
}

} // namespace

int main() {
    buttonModeTracksSerialOpenState();
    rtsDisabledWhenLineControlBusy();
    rtsDisabledWhenHardwareFlowControlOwnsIt();
    rtsEnabledWhenManualControlIsAvailable();

    std::cout << "native_connection_ui_state_tests passed\n";
    return 0;
}
