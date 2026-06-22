#include "win32/native_connection_ui_state.h"

namespace svm::win32 {

NativeConnectionButtonMode NativeConnectionUiState::buttonMode(bool serialOpen) const noexcept {
    return serialOpen ? NativeConnectionButtonMode::Disconnect : NativeConnectionButtonMode::Connect;
}

NativeLineControlUiState NativeConnectionUiState::lineControlState(
    bool lineControlAllowed,
    bool serialOpen,
    bool hardwareRtsCtsSelected,
    bool hardwareRtsCtsActive) const noexcept {
    if (!lineControlAllowed) {
        return {false};
    }
    const bool hardwareManaged = serialOpen ? hardwareRtsCtsActive : hardwareRtsCtsSelected;
    return {!hardwareManaged};
}

} // namespace svm::win32
