#pragma once

#include "win32/native_send_control_state.h"
#include "win32/native_serial_io_state.h"

#include <cstddef>
#include <string_view>

namespace svm::win32 {

enum class NativeSerialSendDecisionKind {
    Proceed,
    Ignore,
    SerialNotConnected,
    SerialIoBusy,
    PayloadInvalid,
    PayloadEmpty,
    QuickSendEmpty,
    FilePathEmpty,
    FileDisconnected,
};

struct NativeSerialSendDecision {
    NativeSerialSendDecisionKind kind = NativeSerialSendDecisionKind::Ignore;
    NativeSerialIoOwner owner = NativeSerialIoOwner::None;

    bool allowed() const noexcept;
    bool ignored() const noexcept;
};

class NativeSerialSendController final {
public:
    NativeSerialSendDecision manualSendAvailability(bool serialOpen, const NativeSerialIoState& ioState) const noexcept;
    NativeSerialSendDecision manualPayloadDecision(bool payloadHasError, bool payloadEmpty) const noexcept;
    NativeSerialSendDecision manualAcquireDecision(bool acquired) const noexcept;

    NativeSerialSendDecision quickSendDecision(
        const NativeSendControlState& sendState,
        std::size_t index,
        std::size_t slotCount,
        std::wstring_view text) const noexcept;

    NativeTimedSendTimerDecision timedSendDecision(
        const NativeSendControlState& sendState,
        bool serialOpen,
        const NativeSerialIoState& ioState,
        int requestedPeriodMs) const noexcept;

    NativeSerialSendDecision fileStartDecision(
        bool serialOpen,
        const NativeSerialIoState& ioState,
        bool fileActive,
        std::wstring_view pathText) const noexcept;
    NativeSerialSendDecision filePumpDecision(
        bool fileActive,
        bool serialOpen,
        const NativeSerialIoState& ioState) const noexcept;
    NativeSerialSendDecision fileAcquireDecision(bool acquired) const noexcept;
};

} // namespace svm::win32
