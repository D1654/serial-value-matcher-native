#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "win32/resource.h"

#include <array>
#include <cstddef>

namespace svm::win32 {

inline constexpr UINT kNativeUiFrameMessage = WM_APP + 17;
inline constexpr UINT kNativeWorkbenchTabRepaintMessage = WM_APP + 20;

inline constexpr std::array<UINT_PTR, 8> kNativeMainWindowShellTimerIds = {
    IDT_SERIAL_POLL,
    IDT_RECONNECT,
    IDT_LOG_FILTER,
    IDT_TIMED_SEND,
    IDT_FILE_SEND,
    IDT_STATUS_CLOCK,
    IDT_LOG_FLUSH,
    IDT_UI_PREFERENCES_SAVE,
};

struct NativeMainWindowShellContext {
    // Borrowed UI-thread handles. NativeMainWindow owns their lifetime.
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HMENU menu = nullptr;
};

enum class NativeMainCommandDomain {
    quick,
    control,
    menu,
    unknown,
};

enum class NativeMainControlCommandDomain {
    serial,
    log,
    send,
    file,
    analysis,
    unknown,
};

enum class NativeMainMenuCommandDomain {
    file,
    serial,
    tools,
    analysis,
    view,
    help,
    unknown,
};

inline bool nativeMainWindowShellReady(const NativeMainWindowShellContext& context) noexcept {
    return context.window != nullptr;
}

inline bool nativeMainWindowIsQuickCommand(WORD commandId, std::size_t quickSlotCount) noexcept {
    return (commandId >= IDC_QUICK_SEND_BUTTON_BASE
               && commandId < IDC_QUICK_SEND_BUTTON_BASE + quickSlotCount)
        || (commandId >= IDC_QUICK_SEND_EDIT_BASE
            && commandId < IDC_QUICK_SEND_EDIT_BASE + quickSlotCount);
}

inline NativeMainControlCommandDomain nativeMainControlCommandDomain(WORD commandId) noexcept {
    switch (commandId) {
    case IDC_REFRESH_BUTTON:
    case IDC_CONNECT_BUTTON:
    case IDC_DISCONNECT_BUTTON:
    case IDC_DTR_CHECK:
    case IDC_RTS_CHECK:
    case IDC_FLOW_CONTROL_COMBO:
    case IDC_SAVE_PROFILE_BUTTON:
        return NativeMainControlCommandDomain::serial;

    case IDC_CLEAR_BUTTON:
    case IDC_LOG_FORMAT_COMBO:
    case IDC_LOG_ENCODING_COMBO:
    case IDC_LOG_CACHE_COMBO:
    case IDC_RAW_EVENT_RETENTION_COMBO:
    case IDC_LOG_FILTER_EDIT:
    case IDC_LOG_SEARCH_EDIT:
    case IDC_LOG_FIND_BUTTON:
    case IDC_COPY_LOG_BUTTON:
    case IDC_EXPORT_LOG_BUTTON:
    case IDC_PAUSE_SCROLL_BUTTON:
        return NativeMainControlCommandDomain::log;

    case IDC_SEND_BUTTON:
    case IDC_SEND_MODE_COMBO:
    case IDC_TEXT_ENCODING_COMBO:
    case IDC_LINE_ENDING_COMBO:
    case IDC_TIMED_SEND_CHECK:
    case IDC_TIMED_PERIOD_EDIT:
    case IDC_HISTORY_COMBO:
        return NativeMainControlCommandDomain::send;

    case IDC_FILE_BROWSE_BUTTON:
    case IDC_FILE_SEND_BUTTON:
    case IDC_FILE_STOP_BUTTON:
    case IDC_FILE_DELAY_COMBO:
        return NativeMainControlCommandDomain::file;

    case IDC_MODBUS_BUTTON:
    case IDC_ANALYSIS_BUTTON:
    case IDC_RULE_VERIFY_BUTTON:
    case IDC_EXPORT_REPORT_BUTTON:
        return NativeMainControlCommandDomain::analysis;

    default:
        return NativeMainControlCommandDomain::unknown;
    }
}

inline NativeMainMenuCommandDomain nativeMainMenuCommandDomain(WORD commandId) noexcept {
    switch (commandId) {
    case IDM_FILE_SAVE_PROFILE:
    case IDM_FILE_EXIT:
        return NativeMainMenuCommandDomain::file;

    case IDM_SERIAL_REFRESH:
    case IDM_SERIAL_CONNECT:
    case IDM_SERIAL_DISCONNECT:
    case IDM_SERIAL_AUTO_RECONNECT:
        return NativeMainMenuCommandDomain::serial;

    case IDM_TOOLS_SEND:
    case IDM_TOOLS_PAUSE_SCROLL:
    case IDM_TOOLS_FOLLOW_LATEST_LOG:
    case IDM_TOOLS_CLEAR_LOG:
    case IDM_TOOLS_COPY_LOG:
    case IDM_TOOLS_EXPORT_LOG:
    case IDM_TOOLS_FIND_LOG:
    case IDM_TOOLS_EXPORT_EVIDENCE_BUNDLE:
        return NativeMainMenuCommandDomain::tools;

    case IDM_ANALYSIS_MODBUS_SCAN:
    case IDM_ANALYSIS_WORKSPACE:
    case IDM_ANALYSIS_RULE_VERIFY:
    case IDM_ANALYSIS_EXPORT_REPORT:
        return NativeMainMenuCommandDomain::analysis;

    case IDM_VIEW_THEME_DEFAULT:
    case IDM_VIEW_THEME_SOFT:
    case IDM_VIEW_THEME_HIGH_CONTRAST:
    case IDM_VIEW_SHOW_TIMESTAMPS:
        return NativeMainMenuCommandDomain::view;

    case IDM_HELP_ABOUT:
        return NativeMainMenuCommandDomain::help;

    default:
        return NativeMainMenuCommandDomain::unknown;
    }
}

inline NativeMainCommandDomain nativeMainCommandDomain(WORD commandId, std::size_t quickSlotCount) noexcept {
    if (nativeMainWindowIsQuickCommand(commandId, quickSlotCount)) {
        return NativeMainCommandDomain::quick;
    }
    if (nativeMainControlCommandDomain(commandId) != NativeMainControlCommandDomain::unknown) {
        return NativeMainCommandDomain::control;
    }
    if (nativeMainMenuCommandDomain(commandId) != NativeMainMenuCommandDomain::unknown) {
        return NativeMainCommandDomain::menu;
    }
    return NativeMainCommandDomain::unknown;
}

} // namespace svm::win32

#endif
