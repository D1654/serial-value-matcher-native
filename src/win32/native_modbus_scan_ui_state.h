#pragma once

namespace svm::win32 {

enum class NativeModbusScanButtonMode {
    Scan,
    Stop,
};

struct NativeModbusScanUiSnapshot {
    NativeModbusScanButtonMode buttonMode = NativeModbusScanButtonMode::Scan;
    bool exclusiveControlsEnabled = true;
};

class NativeModbusScanUiState final {
public:
    NativeModbusScanUiSnapshot snapshot(bool running) const noexcept;
};

} // namespace svm::win32
