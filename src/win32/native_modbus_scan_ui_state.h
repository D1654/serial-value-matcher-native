#pragma once

#include "transport/serial_types.h"

#include <string_view>

namespace svm::win32 {

enum class NativeModbusScanButtonMode {
    Scan,
    Stop,
};

enum class NativeModbusAttemptResultKind {
    Success,
    Timeout,
    RetryExhausted,
    ModbusException,
    FrameError,
    DataFormatError,
    ProtocolMismatch,
    TransportError,
    Cancelled,
    Unknown,
};

struct NativeModbusScanUiSnapshot {
    NativeModbusScanButtonMode buttonMode = NativeModbusScanButtonMode::Scan;
    bool exclusiveControlsEnabled = true;
};

class NativeModbusScanUiState final {
public:
    NativeModbusScanUiSnapshot snapshot(bool running) const noexcept;
};

NativeModbusAttemptResultKind classifyNativeModbusAttemptResult(
    std::string_view status,
    bool isModbusException,
    std::string_view errorMessage) noexcept;
const wchar_t* nativeModbusAttemptResultLabel(NativeModbusAttemptResultKind kind) noexcept;
bool nativeModbusAttemptCountsAsSuccess(NativeModbusAttemptResultKind kind) noexcept;
bool nativeModbusAttemptCountsAsFailure(NativeModbusAttemptResultKind kind) noexcept;
bool nativeModbusScanMessageMatchesSession(
    svm::transport::SerialSessionGeneration messageGeneration,
    svm::transport::SerialSessionGeneration activeScanGeneration,
    const svm::transport::SerialSessionSnapshot& session,
    bool allowFaulted) noexcept;

} // namespace svm::win32
