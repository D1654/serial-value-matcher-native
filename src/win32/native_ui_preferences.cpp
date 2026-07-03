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

int nativeNormalizeWorkbenchHeight(int workbenchHeight) noexcept {
    if (workbenchHeight <= 0) {
        return kNativeDefaultWorkbenchHeight;
    }
    return std::clamp(workbenchHeight, kNativeMinWorkbenchHeight, kNativeMaxWorkbenchHeight);
}

int nativeNormalizeWindowWidth(int windowWidth) noexcept {
    if (windowWidth <= 0) {
        return kNativeDefaultWindowWidth;
    }
    return std::clamp(windowWidth, kNativeMinWindowWidth, kNativeMaxWindowWidth);
}

int nativeNormalizeWindowHeight(int windowHeight) noexcept {
    if (windowHeight <= 0) {
        return kNativeDefaultWindowHeight;
    }
    return std::clamp(windowHeight, kNativeMinWindowHeight, kNativeMaxWindowHeight);
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
    preferences.workbenchHeight = nativeNormalizeWorkbenchHeight(preferences.workbenchHeight);
    preferences.windowWidth = nativeNormalizeWindowWidth(preferences.windowWidth);
    preferences.windowHeight = nativeNormalizeWindowHeight(preferences.windowHeight);
    preferences.quickSendSlots = nativeNormalizeQuickSendSlots(std::move(preferences.quickSendSlots), quickSendSlotCount);
    return preferences;
}

bool nativeUiPreferencesSameSettings(const native_storage::UiPreferences& left, const native_storage::UiPreferences& right) {
    return left.name == right.name
        && left.logThemeIndex == right.logThemeIndex
        && left.logFormat == right.logFormat
        && left.logEncodingCodePage == right.logEncodingCodePage
        && left.showLogTimestamps == right.showLogTimestamps
        && left.sendPayloadMode == right.sendPayloadMode
        && left.sendTextEncodingCodePage == right.sendTextEncodingCodePage
        && left.sendLineEnding == right.sendLineEnding
        && left.autoReconnect == right.autoReconnect
        && left.timedSendEnabled == right.timedSendEnabled
        && left.timedSendPeriodMs == right.timedSendPeriodMs
        && left.fileSendDelayMs == right.fileSendDelayMs
        && left.logVisibleCharLimit == right.logVisibleCharLimit
        && left.rawEventRetentionLimitMb == right.rawEventRetentionLimitMb
        && left.workbenchHeight == right.workbenchHeight
        && left.quickSendSlots == right.quickSendSlots
        && left.windowLeft == right.windowLeft
        && left.windowTop == right.windowTop
        && left.windowWidth == right.windowWidth
        && left.windowHeight == right.windowHeight;
}

} // namespace svm::win32
