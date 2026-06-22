#include "win32/native_modbus_scan_ui_state.h"

namespace svm::win32 {

NativeModbusScanUiSnapshot NativeModbusScanUiState::snapshot(bool running) const noexcept {
    return {
        running ? NativeModbusScanButtonMode::Stop : NativeModbusScanButtonMode::Scan,
        !running,
    };
}

} // namespace svm::win32
