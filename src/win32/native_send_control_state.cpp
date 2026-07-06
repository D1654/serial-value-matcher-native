#include "win32/native_send_control_state.h"

namespace svm::win32 {

void NativeSendControlState::setTimedSendEnabled(bool enabled) noexcept {
    timedSendEnabled_ = enabled;
}

bool NativeSendControlState::timedSendEnabled() const noexcept {
    return timedSendEnabled_;
}

bool NativeSendControlState::canRunTimedSend(bool serialOpen, bool manualSendAllowed) const noexcept {
    return timedSendEnabled_ && serialOpen && manualSendAllowed;
}

NativeTimedSendTimerDecision NativeSendControlState::timerDecision(bool serialOpen, bool manualSendAllowed, int requestedPeriodMs) const noexcept {
    if (!canRunTimedSend(serialOpen, manualSendAllowed)) {
        return {false, nativeNormalizeTimedSendPeriodMs(requestedPeriodMs)};
    }
    return {true, nativeNormalizeTimedSendPeriodMs(requestedPeriodMs)};
}

bool NativeSendControlState::isQuickSendIndexValid(std::size_t index, std::size_t slotCount) const noexcept {
    return index < slotCount;
}

bool NativeSendControlState::isQuickSendTextUsable(std::wstring_view text) const noexcept {
    return !text.empty();
}

} // namespace svm::win32
