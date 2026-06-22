#include "win32/native_ui_preferences.h"

#include <algorithm>
#include <utility>

namespace svm::win32 {

int nativeNormalizeTimedSendPeriodMs(int periodMs) noexcept {
    return std::clamp(periodMs, kNativeMinTimedSendPeriodMs, kNativeMaxTimedSendPeriodMs);
}

int nativeNormalizeFileSendDelayMs(int delayMs) noexcept {
    return std::clamp(delayMs, kNativeMinFileSendDelayMs, kNativeMaxFileSendDelayMs);
}

int nativeNormalizeRawEventRetentionMb(int value) noexcept {
    switch (value) {
    case 0:
    case 100:
    case 500:
    case 1000:
        return value;
    default:
        return kNativeDefaultRawEventRetentionMb;
    }
}

std::size_t nativeNormalizeLogVisibleCharLimit(std::size_t charLimit) noexcept {
    return std::clamp<std::size_t>(charLimit, kNativeMinLogVisibleChars, kNativeMaxLogVisibleChars);
}

std::vector<std::string> nativeNormalizeQuickSendSlots(std::vector<std::string> slots, std::size_t slotCount) {
    slots.resize(slotCount);
    return slots;
}

native_storage::UiPreferences nativeNormalizeUiPreferences(native_storage::UiPreferences preferences, std::size_t quickSendSlotCount) {
    preferences.timedSendPeriodMs = nativeNormalizeTimedSendPeriodMs(preferences.timedSendPeriodMs);
    preferences.fileSendDelayMs = nativeNormalizeFileSendDelayMs(preferences.fileSendDelayMs);
    const std::size_t visibleCharLimit = preferences.logVisibleCharLimit <= 0
        ? 0
        : static_cast<std::size_t>(preferences.logVisibleCharLimit);
    preferences.logVisibleCharLimit = static_cast<int>(nativeNormalizeLogVisibleCharLimit(visibleCharLimit));
    preferences.rawEventRetentionLimitMb = nativeNormalizeRawEventRetentionMb(preferences.rawEventRetentionLimitMb);
    preferences.quickSendSlots = nativeNormalizeQuickSendSlots(std::move(preferences.quickSendSlots), quickSendSlotCount);
    return preferences;
}

} // namespace svm::win32
