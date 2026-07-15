#include "win32/native_modbus_scan_ui_state.h"

namespace svm::win32 {
namespace {

bool contains(std::string_view text, std::string_view pattern) noexcept {
    return text.find(pattern) != std::string_view::npos;
}

bool isFrameError(std::string_view errorMessage) noexcept {
    return contains(errorMessage, "CRC")
        || contains(errorMessage, "RTU frame")
        || contains(errorMessage, "帧")
        || contains(errorMessage, "校验");
}

bool isDataFormatError(std::string_view errorMessage) noexcept {
    return contains(errorMessage, "字节数")
        || contains(errorMessage, "寄存器数量")
        || contains(errorMessage, "地址范围")
        || contains(errorMessage, "byte count")
        || contains(errorMessage, "register count")
        || contains(errorMessage, "payload")
        || contains(errorMessage, "length");
}

bool isProtocolMismatch(std::string_view errorMessage) noexcept {
    return contains(errorMessage, "从站 ID 不匹配")
        || contains(errorMessage, "功能码不匹配")
        || contains(errorMessage, "slave id")
        || contains(errorMessage, "function code");
}

} // namespace

NativeModbusScanUiSnapshot NativeModbusScanUiState::snapshot(bool running) const noexcept {
    return {
        running ? NativeModbusScanButtonMode::Stop : NativeModbusScanButtonMode::Scan,
        !running,
    };
}

NativeModbusAttemptResultKind classifyNativeModbusAttemptResult(
    std::string_view status,
    bool isModbusException,
    std::string_view errorMessage) noexcept {
    if (status == "success" || status == "Success") {
        return NativeModbusAttemptResultKind::Success;
    }
    if (status == "cancelled" || status == "Cancelled") {
        return NativeModbusAttemptResultKind::Cancelled;
    }
    if (status == "timeout" || status == "Timeout") {
        return NativeModbusAttemptResultKind::Timeout;
    }
    if (status == "retry-exhausted" || status == "RetryExhausted") {
        return NativeModbusAttemptResultKind::RetryExhausted;
    }
    if (isModbusException || status == "modbus-exception" || status == "ModbusException") {
        return NativeModbusAttemptResultKind::ModbusException;
    }
    if (status == "transport-error"
        || status == "TransportError"
        || status == "write-error"
        || status == "read-error") {
        return NativeModbusAttemptResultKind::TransportError;
    }
    if (status == "parse-error" || status == "ParseError") {
        if (isFrameError(errorMessage)) {
            return NativeModbusAttemptResultKind::FrameError;
        }
        if (isProtocolMismatch(errorMessage)) {
            return NativeModbusAttemptResultKind::ProtocolMismatch;
        }
        if (isDataFormatError(errorMessage)) {
            return NativeModbusAttemptResultKind::DataFormatError;
        }
        return NativeModbusAttemptResultKind::DataFormatError;
    }

    return NativeModbusAttemptResultKind::Unknown;
}

const wchar_t* nativeModbusAttemptResultLabel(NativeModbusAttemptResultKind kind) noexcept {
    switch (kind) {
    case NativeModbusAttemptResultKind::Success:
        return L"成功";
    case NativeModbusAttemptResultKind::Timeout:
        return L"响应超时";
    case NativeModbusAttemptResultKind::RetryExhausted:
        return L"重试耗尽";
    case NativeModbusAttemptResultKind::ModbusException:
        return L"Modbus 异常";
    case NativeModbusAttemptResultKind::FrameError:
        return L"帧校验错误";
    case NativeModbusAttemptResultKind::DataFormatError:
        return L"数据格式错误";
    case NativeModbusAttemptResultKind::ProtocolMismatch:
        return L"协议不匹配";
    case NativeModbusAttemptResultKind::TransportError:
        return L"传输错误";
    case NativeModbusAttemptResultKind::Cancelled:
        return L"已取消";
    case NativeModbusAttemptResultKind::Unknown:
        return L"未知结果";
    }

    return L"未知结果";
}

bool nativeModbusAttemptCountsAsSuccess(NativeModbusAttemptResultKind kind) noexcept {
    return kind == NativeModbusAttemptResultKind::Success;
}

bool nativeModbusAttemptCountsAsFailure(NativeModbusAttemptResultKind kind) noexcept {
    switch (kind) {
    case NativeModbusAttemptResultKind::Success:
    case NativeModbusAttemptResultKind::Cancelled:
        return false;
    case NativeModbusAttemptResultKind::Timeout:
    case NativeModbusAttemptResultKind::RetryExhausted:
    case NativeModbusAttemptResultKind::ModbusException:
    case NativeModbusAttemptResultKind::FrameError:
    case NativeModbusAttemptResultKind::DataFormatError:
    case NativeModbusAttemptResultKind::ProtocolMismatch:
    case NativeModbusAttemptResultKind::TransportError:
    case NativeModbusAttemptResultKind::Unknown:
        return true;
    }

    return true;
}

bool nativeModbusScanMessageMatchesSession(
    svm::transport::SerialSessionGeneration messageGeneration,
    svm::transport::SerialSessionGeneration activeScanGeneration,
    const svm::transport::SerialSessionSnapshot& session,
    bool allowFaulted) noexcept {
    if (messageGeneration == svm::transport::kUnassignedSerialSessionGeneration
        || messageGeneration != activeScanGeneration) {
        return false;
    }
    if (session.open()) {
        return session.generation == messageGeneration;
    }
    return allowFaulted && session.state == svm::transport::SerialSessionState::Faulted;
}

} // namespace svm::win32
