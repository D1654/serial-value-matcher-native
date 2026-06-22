#pragma once

namespace svm::win32 {

enum class NativeConnectionButtonMode {
    Connect,
    Disconnect,
};

struct NativeLineControlUiState {
    bool rtsEnabled = true;
};

class NativeConnectionUiState final {
public:
    NativeConnectionButtonMode buttonMode(bool serialOpen) const noexcept;
    NativeLineControlUiState lineControlState(
        bool lineControlAllowed,
        bool serialOpen,
        bool hardwareRtsCtsSelected,
        bool hardwareRtsCtsActive) const noexcept;
};

} // namespace svm::win32
