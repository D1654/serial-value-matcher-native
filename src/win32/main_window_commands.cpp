#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_ui_preferences.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/win32_serial_types.h"

#include <algorithm>
#include <optional>
#include <string>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

std::wstring formatLogCacheLimit(std::size_t charLimit) {
    if (charLimit >= 1000000 && charLimit % 1000000 == 0) {
        return std::to_wstring(charLimit / 1000000) + L"M";
    }
    if (charLimit >= 1000 && charLimit % 1000 == 0) {
        return std::to_wstring(charLimit / 1000) + L"K";
    }
    return std::to_wstring(charLimit);
}

} // namespace

std::optional<LRESULT> NativeMainWindow::handleCommandMessage(WPARAM wParam) {
    const WORD commandId = LOWORD(wParam);
    const WORD notificationCode = HIWORD(wParam);

    switch (nativeMainCommandDomain(commandId, quickSendButtons_.size())) {
    case NativeMainCommandDomain::quick:
        return handleQuickCommand(commandId, notificationCode);
    case NativeMainCommandDomain::control:
        return handleControlCommand(commandId, notificationCode);
    case NativeMainCommandDomain::menu:
        return handleMenuCommand(commandId);
    case NativeMainCommandDomain::unknown:
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleQuickCommand(WORD commandId, WORD notificationCode) {
    if (commandId >= IDC_QUICK_SEND_BUTTON_BASE && commandId < IDC_QUICK_SEND_BUTTON_BASE + quickSendButtons_.size()) {
        if (notificationCode == BN_CLICKED) {
            sendQuickPayload(static_cast<std::size_t>(commandId - IDC_QUICK_SEND_BUTTON_BASE));
        }
        return 0;
    }
    if (commandId >= IDC_QUICK_SEND_EDIT_BASE && commandId < IDC_QUICK_SEND_EDIT_BASE + quickSendEdits_.size()) {
        if (notificationCode == EN_KILLFOCUS) {
            saveUiPreferences();
        }
        return 0;
    }
    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleControlCommand(WORD commandId, WORD notificationCode) {
    switch (nativeMainControlCommandDomain(commandId)) {
    case NativeMainControlCommandDomain::serial:
        return handleSerialControlCommand(commandId, notificationCode);
    case NativeMainControlCommandDomain::log:
        return handleLogControlCommand(commandId, notificationCode);
    case NativeMainControlCommandDomain::send:
        return handleSendControlCommand(commandId, notificationCode);
    case NativeMainControlCommandDomain::file:
        return handleFileControlCommand(commandId, notificationCode);
    case NativeMainControlCommandDomain::analysis:
        return handleAnalysisControlCommand(commandId);
    case NativeMainControlCommandDomain::unknown:
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleSerialControlCommand(WORD commandId, WORD notificationCode) {
    switch (commandId) {
    case IDC_REFRESH_BUTTON:
        refreshPorts();
        return 0;
    case IDC_CONNECT_BUTTON:
        toggleConnection();
        return 0;
    case IDC_DISCONNECT_BUTTON:
        disconnectSerial();
        return 0;
    case IDC_DTR_CHECK:
    case IDC_RTS_CHECK:
        if (notificationCode == BN_CLICKED) {
            applySerialLineControl(commandId);
        }
        return 0;
    case IDC_AUTO_RECONNECT_CHECK:
        if (notificationCode == BN_CLICKED) {
            applyAutoReconnectPreference();
        }
        return 0;
    case IDC_FLOW_CONTROL_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            updateRtsControlState();
            saveUiPreferences();
            const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
            if ((!snapshot.open()
                    && selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None))
                        == static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts))
                || (snapshot.open() && snapshot.usesHardwareRtsCts())) {
                setStatus(tx(T::RtsHardwareManaged));
            }
        }
        return 0;
    case IDC_SAVE_PROFILE_BUTTON:
        saveCurrentSerialProfile();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleLogControlCommand(WORD commandId, WORD notificationCode) {
    switch (commandId) {
    case IDC_CLEAR_BUTTON:
        clearLog();
        return 0;
    case IDC_LOG_FORMAT_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            rebuildLogView();
            saveUiPreferences();
            setStatus(tx(T::LogFormatChanged));
        }
        return 0;
    case IDC_LOG_ENCODING_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            rebuildLogView();
            saveUiPreferences();
            setStatus(tx(T::LogEncodingChanged));
        }
        return 0;
    case IDC_LOG_CACHE_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            applyLogCacheLimit(static_cast<std::size_t>(selectedComboData(logCacheCombo_, kNativeDefaultLogVisibleChars)));
            rebuildLogView();
            saveUiPreferences();
            std::wstring status = uiString(T::LogCacheChangedPrefix) + formatLogCacheLimit(logVisibleCharLimit_);
            if (logVisibleCharLimit_ >= 50000000) {
                status += tx(T::LogCacheLargeSuffix);
            }
            status += tx(T::ChinesePeriod);
            setStatus(status);
        }
        return 0;
    case IDC_RAW_EVENT_RETENTION_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            const int retentionLimitMb = static_cast<int>(selectedComboData(rawEventRetentionCombo_, kNativeDefaultRawEventRetentionMb));
            applyRawEventRetentionLimit(retentionLimitMb);
            saveUiPreferences();
            setStatus(retentionLimitMb <= 0
                ? L"\u539F\u59CB\u8BB0\u5F55\u4FDD\u7559\u4E0A\u9650\u5DF2\u8BBE\u4E3A\u4E0D\u9650\u5236\u3002"
                : (L"\u539F\u59CB\u8BB0\u5F55\u4FDD\u7559\u4E0A\u9650\u5DF2\u8BBE\u4E3A "
                    + std::to_wstring(retentionLimitMb)
                    + L"M\u3002"));
        }
        return 0;
    case IDC_LOG_FILTER_EDIT:
        if (notificationCode == EN_CHANGE) {
            KillTimer(window_, IDT_LOG_FILTER);
            SetTimer(window_, IDT_LOG_FILTER, 180, nullptr);
        }
        return 0;
    case IDC_LOG_SEARCH_EDIT:
        if (notificationCode == EN_CHANGE) {
            logFilterState_.resetSearch();
        }
        return 0;
    case IDC_LOG_FIND_BUTTON:
        findNextLogMatch();
        return 0;
    case IDC_COPY_LOG_BUTTON:
        copyVisibleLogToClipboard();
        return 0;
    case IDC_EXPORT_LOG_BUTTON:
        exportVisibleLog();
        return 0;
    case IDC_PAUSE_SCROLL_BUTTON:
        toggleLogScrollPause();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleSendControlCommand(WORD commandId, WORD notificationCode) {
    switch (commandId) {
    case IDC_SEND_BUTTON:
        sendPayload();
        return 0;
    case IDC_SEND_MODE_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            saveUiPreferences();
            setSendModeStatus();
        }
        return 0;
    case IDC_TEXT_ENCODING_COMBO:
    case IDC_LINE_ENDING_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            saveUiPreferences();
        }
        return 0;
    case IDC_TIMED_SEND_CHECK:
        if (notificationCode == BN_CLICKED) {
            sendControlState_.setTimedSendEnabled(SendMessageW(timedSendCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (!sendControlState_.timedSendEnabled()) {
                timedSendConfirmed_ = false;
            }
            updateTimedSendTimer();
            saveUiPreferences();
            setStatus(sendControlState_.timedSendEnabled() ? tx(T::TimedSendEnabledStatus) : tx(T::TimedSendDisabledStatus));
        }
        return 0;
    case IDC_TIMED_PERIOD_EDIT:
        if (notificationCode == EN_CHANGE && sendControlState_.timedSendEnabled()) {
            updateTimedSendTimer();
        } else if (notificationCode == EN_KILLFOCUS) {
            saveUiPreferences();
        }
        return 0;
    case IDC_HISTORY_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            applySelectedHistory();
        }
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleFileControlCommand(WORD commandId, WORD notificationCode) {
    switch (commandId) {
    case IDC_FILE_BROWSE_BUTTON:
        browseFileSend();
        return 0;
    case IDC_FILE_SEND_BUTTON:
        startFileSend();
        return 0;
    case IDC_FILE_STOP_BUTTON:
        stopFileSend();
        return 0;
    case IDC_FILE_DELAY_COMBO:
        if (notificationCode == CBN_SELCHANGE) {
            if (fileSend_.active()) {
                KillTimer(window_, IDT_FILE_SEND);
                SetTimer(window_, IDT_FILE_SEND, static_cast<UINT>(std::max<int>(1, static_cast<int>(selectedComboData(fileDelayCombo_, 0)))), nullptr);
            }
            saveUiPreferences();
        }
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleAnalysisControlCommand(WORD commandId) {
    switch (commandId) {
    case IDC_MODBUS_BUTTON:
        runModbusScan();
        return 0;
    case IDC_ANALYSIS_BUTTON:
        showAnalysisWorkspace();
        return 0;
    case IDC_RULE_VERIFY_BUTTON:
        showRuleVerification();
        return 0;
    case IDC_EXPORT_REPORT_BUTTON:
        exportReport();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleMenuCommand(WORD commandId) {
    switch (nativeMainMenuCommandDomain(commandId)) {
    case NativeMainMenuCommandDomain::file:
        return handleFileMenuCommand(commandId);
    case NativeMainMenuCommandDomain::serial:
        return handleSerialMenuCommand(commandId);
    case NativeMainMenuCommandDomain::tools:
        return handleToolsMenuCommand(commandId);
    case NativeMainMenuCommandDomain::analysis:
        return handleAnalysisMenuCommand(commandId);
    case NativeMainMenuCommandDomain::view:
        return handleViewMenuCommand(commandId);
    case NativeMainMenuCommandDomain::help:
        return handleHelpMenuCommand(commandId);
    case NativeMainMenuCommandDomain::unknown:
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleFileMenuCommand(WORD commandId) {
    switch (commandId) {
    case IDM_FILE_SAVE_PROFILE:
        saveCurrentSerialProfile();
        return 0;
    case IDM_FILE_EXIT:
        DestroyWindow(window_);
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleSerialMenuCommand(WORD commandId) {
    switch (commandId) {
    case IDM_SERIAL_REFRESH:
        refreshPorts();
        return 0;
    case IDM_SERIAL_CONNECT:
        connectSerial();
        return 0;
    case IDM_SERIAL_DISCONNECT:
        disconnectSerial();
        return 0;
    case IDM_SERIAL_AUTO_RECONNECT:
        SendMessageW(
            autoReconnectCheck_,
            BM_SETCHECK,
            SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED,
            0);
        applyAutoReconnectPreference();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

void NativeMainWindow::applyAutoReconnectPreference() {
    const bool enabled = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!enabled) {
        reconnectState_.clearWaiting();
        KillTimer(window_, IDT_RECONNECT);
    }
    setStatus(enabled ? tx(T::AutoReconnectEnabled) : tx(T::AutoReconnectDisabled));
    saveUiPreferences();
}

std::optional<LRESULT> NativeMainWindow::handleToolsMenuCommand(WORD commandId) {
    switch (commandId) {
    case IDM_TOOLS_SEND:
        sendPayload();
        return 0;
    case IDM_TOOLS_PAUSE_SCROLL:
        toggleLogScrollPause();
        return 0;
    case IDM_TOOLS_FOLLOW_LATEST_LOG:
        followLatestLog();
        return 0;
    case IDM_TOOLS_CLEAR_LOG:
        clearLog();
        return 0;
    case IDM_TOOLS_COPY_LOG:
        copyVisibleLogToClipboard();
        return 0;
    case IDM_TOOLS_EXPORT_LOG:
        exportVisibleLog();
        return 0;
    case IDM_TOOLS_EXPORT_EVIDENCE_BUNDLE:
        exportEvidenceBundle();
        return 0;
    case IDM_TOOLS_FIND_LOG:
        findNextLogMatch();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleAnalysisMenuCommand(WORD commandId) {
    switch (commandId) {
    case IDM_ANALYSIS_MODBUS_SCAN:
        runModbusScan();
        return 0;
    case IDM_ANALYSIS_WORKSPACE:
        showAnalysisWorkspace();
        return 0;
    case IDM_ANALYSIS_RULE_VERIFY:
        showRuleVerification();
        return 0;
    case IDM_ANALYSIS_EXPORT_REPORT:
        exportReport();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleViewMenuCommand(WORD commandId) {
    switch (commandId) {
    case IDM_VIEW_THEME_DEFAULT:
        applyLogTheme(0);
        rebuildLogView();
        saveUiPreferences();
        setStatus(tx(T::ThemeDefaultStatus));
        return 0;
    case IDM_VIEW_THEME_SOFT:
        applyLogTheme(1);
        rebuildLogView();
        saveUiPreferences();
        setStatus(tx(T::ThemeSoftStatus));
        return 0;
    case IDM_VIEW_THEME_HIGH_CONTRAST:
        applyLogTheme(2);
        rebuildLogView();
        saveUiPreferences();
        setStatus(tx(T::ThemeHighContrastStatus));
        return 0;
    case IDM_VIEW_SHOW_TIMESTAMPS:
        showLogTimestamps_ = !showLogTimestamps_;
        updateLogTimestampMenu();
        rebuildLogView();
        saveUiPreferences();
        setStatus(showLogTimestamps_ ? tx(T::LogTimestampsEnabledStatus) : tx(T::LogTimestampsDisabledStatus));
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleHelpMenuCommand(WORD commandId) {
    switch (commandId) {
    case IDM_HELP_ABOUT:
        showAbout();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

} // namespace svm::win32

#endif
