#include "win32/native_serial_send_controller.h"

namespace svm::win32 {

bool NativeSerialSendDecision::allowed() const noexcept {
    return kind == NativeSerialSendDecisionKind::Proceed;
}

bool NativeSerialSendDecision::ignored() const noexcept {
    return kind == NativeSerialSendDecisionKind::Ignore;
}

NativeSerialSendDecision NativeSerialSendController::manualSendAvailability(
    bool serialOpen,
    const NativeSerialIoState& ioState) const noexcept {
    if (!serialOpen) {
        return {NativeSerialSendDecisionKind::SerialNotConnected};
    }
    if (!ioState.allowsOwner(NativeSerialIoOwner::ManualSend)) {
        return {NativeSerialSendDecisionKind::SerialIoBusy};
    }
    return {NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::ManualSend};
}

NativeSerialSendDecision NativeSerialSendController::manualPayloadDecision(bool payloadHasError, bool payloadEmpty) const noexcept {
    if (payloadHasError) {
        return {NativeSerialSendDecisionKind::PayloadInvalid};
    }
    if (payloadEmpty) {
        return {NativeSerialSendDecisionKind::PayloadEmpty};
    }
    return {NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::ManualSend};
}

NativeSerialSendDecision NativeSerialSendController::manualAcquireDecision(bool acquired) const noexcept {
    return acquired
        ? NativeSerialSendDecision{NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::ManualSend}
        : NativeSerialSendDecision{NativeSerialSendDecisionKind::SerialIoBusy};
}

NativeSerialSendDecision NativeSerialSendController::quickSendDecision(
    const NativeSendControlState& sendState,
    std::size_t index,
    std::size_t slotCount,
    std::wstring_view text) const noexcept {
    if (!sendState.isQuickSendIndexValid(index, slotCount)) {
        return {NativeSerialSendDecisionKind::Ignore};
    }
    if (!sendState.isQuickSendTextUsable(text)) {
        return {NativeSerialSendDecisionKind::QuickSendEmpty};
    }
    return {NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::ManualSend};
}

NativeTimedSendTimerDecision NativeSerialSendController::timedSendDecision(
    const NativeSendControlState& sendState,
    bool serialOpen,
    const NativeSerialIoState& ioState,
    int requestedPeriodMs) const noexcept {
    return sendState.timerDecision(serialOpen, ioState.allowsOwner(NativeSerialIoOwner::ManualSend), requestedPeriodMs);
}

NativeSerialSendDecision NativeSerialSendController::fileStartDecision(
    bool serialOpen,
    const NativeSerialIoState& ioState,
    bool fileActive,
    std::wstring_view pathText) const noexcept {
    if (!serialOpen) {
        return {NativeSerialSendDecisionKind::SerialNotConnected};
    }
    if (!ioState.allowsOwner(NativeSerialIoOwner::FileSend)) {
        return {NativeSerialSendDecisionKind::SerialIoBusy};
    }
    if (fileActive) {
        return {NativeSerialSendDecisionKind::Ignore};
    }
    if (pathText.empty()) {
        return {NativeSerialSendDecisionKind::FilePathEmpty};
    }
    return {NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::FileSend};
}

NativeSerialSendDecision NativeSerialSendController::filePumpDecision(
    bool fileActive,
    bool serialOpen,
    const NativeSerialIoState& ioState) const noexcept {
    if (!fileActive) {
        return {NativeSerialSendDecisionKind::Ignore};
    }
    if (!serialOpen) {
        return {NativeSerialSendDecisionKind::FileDisconnected};
    }
    if (!ioState.isOwnedBy(NativeSerialIoOwner::FileSend)) {
        return {NativeSerialSendDecisionKind::SerialIoBusy};
    }
    return {NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::FileSend};
}

NativeSerialSendDecision NativeSerialSendController::fileAcquireDecision(bool acquired) const noexcept {
    return acquired
        ? NativeSerialSendDecision{NativeSerialSendDecisionKind::Proceed, NativeSerialIoOwner::FileSend}
        : NativeSerialSendDecision{NativeSerialSendDecisionKind::SerialIoBusy};
}

} // namespace svm::win32
