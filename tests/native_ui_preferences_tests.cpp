#include "win32/native_ui_preferences.h"

#include <cassert>
#include <iostream>

namespace {

void scalarLimitsAreStable() {
    assert(svm::win32::nativeNormalizeTimedSendPeriodMs(1) == svm::win32::kNativeMinTimedSendPeriodMs);
    assert(svm::win32::nativeNormalizeTimedSendPeriodMs(5000) == 5000);
    assert(svm::win32::nativeNormalizeTimedSendPeriodMs(4000000) == svm::win32::kNativeMaxTimedSendPeriodMs);

    assert(svm::win32::nativeNormalizeFileSendDelayMs(-1) == svm::win32::kNativeMinFileSendDelayMs);
    assert(svm::win32::nativeNormalizeFileSendDelayMs(20) == 20);
    assert(svm::win32::nativeNormalizeFileSendDelayMs(2000) == svm::win32::kNativeMaxFileSendDelayMs);

    assert(svm::win32::nativeNormalizeLogVisibleCharLimit(10) == svm::win32::kNativeMinLogVisibleChars);
    assert(svm::win32::nativeNormalizeLogVisibleCharLimit(350000) == 350000);
    assert(svm::win32::nativeNormalizeLogVisibleCharLimit(200000000) == svm::win32::kNativeMaxLogVisibleChars);

    assert(svm::win32::nativeNormalizeWorkbenchHeight(-1) == svm::win32::kNativeDefaultWorkbenchHeight);
    assert(svm::win32::nativeNormalizeWorkbenchHeight(0) == svm::win32::kNativeDefaultWorkbenchHeight);
    assert(svm::win32::nativeNormalizeWorkbenchHeight(20) == svm::win32::kNativeMinWorkbenchHeight);
    assert(svm::win32::nativeNormalizeWorkbenchHeight(320) == 320);
    assert(svm::win32::nativeNormalizeWorkbenchHeight(2000) == svm::win32::kNativeMaxWorkbenchHeight);
}

void rawEventRetentionOnlyAllowsKnownChoices() {
    assert(svm::win32::nativeNormalizeRawEventRetentionMb(0) == 0);
    assert(svm::win32::nativeNormalizeRawEventRetentionMb(100) == 100);
    assert(svm::win32::nativeNormalizeRawEventRetentionMb(500) == 500);
    assert(svm::win32::nativeNormalizeRawEventRetentionMb(1000) == 1000);
    assert(svm::win32::nativeNormalizeRawEventRetentionMb(-10) == svm::win32::kNativeDefaultRawEventRetentionMb);
    assert(svm::win32::nativeNormalizeRawEventRetentionMb(200) == svm::win32::kNativeDefaultRawEventRetentionMb);
}

void windowSizeLimitsPreserveUsableGeometry() {
    assert(svm::win32::nativeNormalizeWindowWidth(-1) == svm::win32::kNativeDefaultWindowWidth);
    assert(svm::win32::nativeNormalizeWindowHeight(-1) == svm::win32::kNativeDefaultWindowHeight);
    assert(svm::win32::nativeNormalizeWindowWidth(100) == svm::win32::kNativeMinWindowWidth);
    assert(svm::win32::nativeNormalizeWindowHeight(100) == svm::win32::kNativeMinWindowHeight);
    assert(svm::win32::nativeNormalizeWindowWidth(900) == 900);
    assert(svm::win32::nativeNormalizeWindowHeight(620) == 620);
    assert(svm::win32::nativeNormalizeWindowWidth(100000) == svm::win32::kNativeMaxWindowWidth);
    assert(svm::win32::nativeNormalizeWindowHeight(100000) == svm::win32::kNativeMaxWindowHeight);
}

void quickSendSlotsArePaddedAndTrimmed() {
    auto padded = svm::win32::nativeNormalizeQuickSendSlots({"A", "B"}, 4);
    assert(padded.size() == 4);
    assert(padded[0] == "A");
    assert(padded[1] == "B");
    assert(padded[2].empty());
    assert(padded[3].empty());

    auto trimmed = svm::win32::nativeNormalizeQuickSendSlots({"0", "1", "2", "3"}, 2);
    assert(trimmed.size() == 2);
    assert(trimmed[0] == "0");
    assert(trimmed[1] == "1");
}

void wholePreferencesAreNormalizedTogether() {
    svm::native_storage::UiPreferences preferences;
    preferences.timedSendPeriodMs = 1;
    preferences.fileSendDelayMs = 9999;
    preferences.logVisibleCharLimit = -5;
    preferences.rawEventRetentionLimitMb = 250;
    preferences.workbenchHeight = 20;
    preferences.windowWidth = 100;
    preferences.windowHeight = -5;
    preferences.quickSendSlots = {"AT"};

    const auto normalized = svm::win32::nativeNormalizeUiPreferences(preferences, 3);
    assert(normalized.timedSendPeriodMs == svm::win32::kNativeMinTimedSendPeriodMs);
    assert(normalized.fileSendDelayMs == svm::win32::kNativeMaxFileSendDelayMs);
    assert(normalized.logVisibleCharLimit == static_cast<int>(svm::win32::kNativeMinLogVisibleChars));
    assert(normalized.rawEventRetentionLimitMb == svm::win32::kNativeDefaultRawEventRetentionMb);
    assert(normalized.workbenchHeight == svm::win32::kNativeMinWorkbenchHeight);
    assert(normalized.windowWidth == svm::win32::kNativeMinWindowWidth);
    assert(normalized.windowHeight == svm::win32::kNativeDefaultWindowHeight);
    assert(normalized.quickSendSlots.size() == 3);
    assert(normalized.quickSendSlots[0] == "AT");
    assert(normalized.quickSendSlots[1].empty());
}

void sameSettingsIgnoreStorageMetadata() {
    svm::native_storage::UiPreferences left;
    left.quickSendSlots = {"A", "B"};
    left.updatedAtUtc = "2026-01-01T00:00:00Z";
    left.id = 1;

    auto right = left;
    right.updatedAtUtc = "2026-01-02T00:00:00Z";
    right.id = 99;
    assert(svm::win32::nativeUiPreferencesSameSettings(left, right));

    right.windowWidth += 1;
    assert(!svm::win32::nativeUiPreferencesSameSettings(left, right));

    right = left;
    right.workbenchHeight = 260;
    assert(!svm::win32::nativeUiPreferencesSameSettings(left, right));

    right = left;
    right.quickSendSlots[1] = "C";
    assert(!svm::win32::nativeUiPreferencesSameSettings(left, right));
}

} // namespace

int main() {
    scalarLimitsAreStable();
    rawEventRetentionOnlyAllowsKnownChoices();
    windowSizeLimitsPreserveUsableGeometry();
    quickSendSlotsArePaddedAndTrimmed();
    wholePreferencesAreNormalizedTogether();
    sameSettingsIgnoreStorageMetadata();

    std::cout << "native_ui_preferences_tests passed\n";
    return 0;
}
