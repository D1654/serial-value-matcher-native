#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_time_utils.h"
#include "win32/native_ui_preferences.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"

#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

void NativeMainWindow::applyUiPreferences() {
    if (!store_.isOpen()) {
        return;
    }
    const auto storedPreferences = store_.latestUiPreferences();
    if (!storedPreferences.has_value()) {
        return;
    }
    const native_storage::UiPreferences preferences = nativeNormalizeUiPreferences(*storedPreferences, quickSendEdits_.size());
    lastSavedUiPreferences_ = preferences;

    selectComboData(logFormatCombo_, preferences.logFormat);
    selectComboData(logEncodingCombo_, preferences.logEncodingCodePage);
    selectComboData(sendModeCombo_, preferences.sendPayloadMode);
    selectComboData(textEncodingCombo_, preferences.sendTextEncodingCodePage);
    selectComboData(lineEndingCombo_, preferences.sendLineEnding);
    SendMessageW(autoReconnectCheck_, BM_SETCHECK, preferences.autoReconnect ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(timedSendCheck_, BM_SETCHECK, preferences.timedSendEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    sendControlState_.setTimedSendEnabled(preferences.timedSendEnabled);
    setControlText(timedPeriodEdit_, std::to_wstring(preferences.timedSendPeriodMs));
    selectComboData(fileDelayCombo_, preferences.fileSendDelayMs);
    applyLogCacheLimit(static_cast<std::size_t>(preferences.logVisibleCharLimit));
    selectComboData(logCacheCombo_, static_cast<LPARAM>(logVisibleCharLimit_));
    applyRawEventRetentionLimit(preferences.rawEventRetentionLimitMb);
    selectComboData(rawEventRetentionCombo_, preferences.rawEventRetentionLimitMb);
    preferredWorkbenchHeight_ = preferences.workbenchHeight;
    for (std::size_t index = 0; index < quickSendEdits_.size() && index < preferences.quickSendSlots.size(); ++index) {
        setControlText(quickSendEdits_[index], utf8ToWide(preferences.quickSendSlots[index]));
    }
    showLogTimestamps_ = preferences.showLogTimestamps;
    applyLogTheme(preferences.logThemeIndex);
    updateLogTimestampMenu();

    if (preferences.windowWidth >= 1080 && preferences.windowHeight >= 760) {
        RECT currentRect = {};
        GetWindowRect(window_, &currentRect);
        MoveWindow(
            window_,
            preferences.windowLeft >= 0 ? preferences.windowLeft : currentRect.left,
            preferences.windowTop >= 0 ? preferences.windowTop : currentRect.top,
            preferences.windowWidth,
            preferences.windowHeight,
            TRUE);
    }
    setStatus(tx(T::UiPreferencesRestoredStatus));
}

void NativeMainWindow::saveUiPreferences() {
    if (!store_.isOpen() || window_ == nullptr) {
        return;
    }

    RECT windowRect = {};
    GetWindowRect(window_, &windowRect);
    native_storage::UiPreferences preferences;
    preferences.name = "default";
    preferences.logThemeIndex = logThemeIndex_;
    preferences.logFormat = static_cast<int>(selectedComboData(logFormatCombo_, 0));
    preferences.logEncodingCodePage = static_cast<int>(selectedLogCodePage());
    preferences.showLogTimestamps = showLogTimestamps_;
    preferences.sendPayloadMode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    preferences.sendTextEncodingCodePage = static_cast<int>(selectedTextCodePage());
    preferences.sendLineEnding = static_cast<int>(selectedComboData(lineEndingCombo_, 0));
    preferences.autoReconnect = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preferences.timedSendEnabled = SendMessageW(timedSendCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    preferences.timedSendPeriodMs = nativeNormalizeTimedSendPeriodMs(textToInt(timedPeriodEdit_, kNativeDefaultTimedSendPeriodMs));
    preferences.fileSendDelayMs = nativeNormalizeFileSendDelayMs(static_cast<int>(selectedComboData(fileDelayCombo_, 0)));
    preferences.logVisibleCharLimit = static_cast<int>(nativeNormalizeLogVisibleCharLimit(logVisibleCharLimit_));
    preferences.rawEventRetentionLimitMb = nativeNormalizeRawEventRetentionMb(
        static_cast<int>(selectedComboData(rawEventRetentionCombo_, kNativeDefaultRawEventRetentionMb)));
    preferences.workbenchHeight = nativeNormalizeWorkbenchHeight(preferredWorkbenchHeight_);
    preferences.quickSendSlots.clear();
    preferences.quickSendSlots.reserve(quickSendEdits_.size());
    for (HWND edit : quickSendEdits_) {
        preferences.quickSendSlots.push_back(wideToUtf8(controlText(edit)));
    }
    preferences.quickSendSlots = nativeNormalizeQuickSendSlots(std::move(preferences.quickSendSlots), quickSendEdits_.size());
    preferences.windowLeft = windowRect.left;
    preferences.windowTop = windowRect.top;
    preferences.windowWidth = windowRect.right - windowRect.left;
    preferences.windowHeight = windowRect.bottom - windowRect.top;
    if (lastSavedUiPreferences_.has_value() && nativeUiPreferencesSameSettings(*lastSavedUiPreferences_, preferences)) {
        return;
    }
    preferences.updatedAtUtc = nativeUtcTimestampText();
    if (!store_.saveUiPreferences(preferences)) {
        if (!uiPreferenceSaveFailureShown_) {
            uiPreferenceSaveFailureShown_ = true;
            setStatus(uiString(T::UiPreferencesSaveFailedPrefix) + utf8ToWide(store_.lastErrorText()));
        }
        return;
    }
    lastSavedUiPreferences_ = preferences;
    uiPreferenceSaveFailureShown_ = false;
}

void NativeMainWindow::applyRawEventRetentionLimit(int retentionLimitMb) {
    if (!store_.isOpen()) {
        return;
    }
    retentionLimitMb = nativeNormalizeRawEventRetentionMb(retentionLimitMb);
    if (retentionLimitMb <= 0) {
        store_.setRawEventRetentionLimit(0, 0);
        return;
    }
    const std::uintmax_t softLimitBytes = static_cast<std::uintmax_t>(retentionLimitMb) * 1024ULL * 1024ULL;
    const std::uintmax_t targetBytes = softLimitBytes * 4ULL / 5ULL;
    store_.setRawEventRetentionLimit(softLimitBytes, targetBytes);
}

} // namespace svm::win32

#endif
