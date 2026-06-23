#pragma once

#include "native_storage/native_session_store.h"

#include <cstddef>
#include <string>
#include <vector>

namespace svm::win32 {

inline constexpr int kNativeDefaultTimedSendPeriodMs = 1000;
inline constexpr int kNativeMinTimedSendPeriodMs = 50;
inline constexpr int kNativeMaxTimedSendPeriodMs = 3600000;
inline constexpr int kNativeMinFileSendDelayMs = 0;
inline constexpr int kNativeMaxFileSendDelayMs = 1000;
inline constexpr int kNativeDefaultRawEventRetentionMb = 100;
inline constexpr std::size_t kNativeQuickSendSlotCount = 10;
inline constexpr std::size_t kNativeMinLogVisibleChars = 200000;
inline constexpr std::size_t kNativeMaxLogVisibleChars = 100000000;
inline constexpr std::size_t kNativeDefaultLogVisibleChars = 350000;

int nativeNormalizeTimedSendPeriodMs(int periodMs) noexcept;
int nativeNormalizeFileSendDelayMs(int delayMs) noexcept;
int nativeNormalizeRawEventRetentionMb(int value) noexcept;
std::size_t nativeNormalizeLogVisibleCharLimit(std::size_t charLimit) noexcept;
std::vector<std::string> nativeNormalizeQuickSendSlots(std::vector<std::string> slots, std::size_t slotCount = kNativeQuickSendSlotCount);
native_storage::UiPreferences nativeNormalizeUiPreferences(native_storage::UiPreferences preferences, std::size_t quickSendSlotCount = kNativeQuickSendSlotCount);
bool nativeUiPreferencesSameSettings(const native_storage::UiPreferences& left, const native_storage::UiPreferences& right);

} // namespace svm::win32
