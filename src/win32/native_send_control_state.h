#pragma once

#include "win32/native_ui_preferences.h"

#include <cstddef>
#include <string_view>

namespace svm::win32 {

struct NativeTimedSendTimerDecision {
    bool shouldRun = false;
    int periodMs = kNativeDefaultTimedSendPeriodMs;
};

class NativeSendControlState final {
public:
    void setTimedSendEnabled(bool enabled) noexcept;
    bool timedSendEnabled() const noexcept;

    bool canRunTimedSend(bool serialOpen, bool manualSendAllowed) const noexcept;
    NativeTimedSendTimerDecision timerDecision(bool serialOpen, bool manualSendAllowed, int requestedPeriodMs) const noexcept;

    bool isQuickSendIndexValid(std::size_t index, std::size_t slotCount) const noexcept;
    bool isQuickSendTextUsable(std::wstring_view text) const noexcept;

private:
    bool timedSendEnabled_ = false;
};

} // namespace svm::win32
