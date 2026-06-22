#include "win32/native_modbus_scan_ui_state.h"

#include <cassert>
#include <iostream>

namespace {

void idleSnapshotKeepsControlsEditable() {
    svm::win32::NativeModbusScanUiState state;
    const auto ui = state.snapshot(false);
    assert(ui.buttonMode == svm::win32::NativeModbusScanButtonMode::Scan);
    assert(ui.exclusiveControlsEnabled);
}

void runningSnapshotSwitchesToStopAndLocksExclusiveControls() {
    svm::win32::NativeModbusScanUiState state;
    const auto ui = state.snapshot(true);
    assert(ui.buttonMode == svm::win32::NativeModbusScanButtonMode::Stop);
    assert(!ui.exclusiveControlsEnabled);
}

} // namespace

int main() {
    idleSnapshotKeepsControlsEditable();
    runningSnapshotSwitchesToStopAndLocksExclusiveControls();

    std::cout << "native_modbus_scan_ui_state_tests passed\n";
    return 0;
}
