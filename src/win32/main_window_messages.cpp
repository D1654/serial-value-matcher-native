#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_ui_preferences.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_types.h"

#include <algorithm>
#include <commctrl.h>
#include <optional>
#include <string>

namespace svm::win32 {
namespace {

using T = TextId;

constexpr COLORREF kFormBackgroundColor = RGB(228, 228, 228);

const wchar_t* tx(T id) {
    return uiText(id);
}

HBRUSH formBackgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(kFormBackgroundColor);
    return brush;
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

LRESULT NativeMainWindow::handleCreateMessage() {
    createMenus();
    createControls();
    if (!store_.open(defaultStoreDirectory())) {
        setStatus(utf8ToWide(store_.lastErrorText()));
    }
    applyUiPreferences();
    refreshPorts();
    applyLatestSerialProfile();
    refreshSendHistory();
    if (const auto run = store_.latestMatchRun(); run.has_value()) {
        candidateCacheState_.setLatestMatchRunId(run->runId);
        refreshCandidateCombo(run->runId);
    }
    if (const auto verificationRun = store_.latestRuleVerificationRun(); verificationRun.has_value()) {
        candidateCacheState_.setLatestVerificationRunId(verificationRun->verificationRunId);
    }
    SetTimer(window_, IDT_SERIAL_POLL, 50, nullptr);
    SetTimer(window_, IDT_STATUS_CLOCK, 1000, nullptr);
    return 0;
}

std::optional<LRESULT> NativeMainWindow::handleNotifyMessage(LPARAM lParam) {
    const auto* notification = reinterpret_cast<NMHDR*>(lParam);
    if (notification != nullptr && notification->idFrom == IDC_WORK_TABS && notification->code == TCN_SELCHANGE) {
        updateWorkbenchTab();
        return 0;
    }
    return std::nullopt;
}

LRESULT NativeMainWindow::handleEditColorMessage(WPARAM wParam) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
}

std::optional<LRESULT> NativeMainWindow::handleStaticColorMessage(WPARAM wParam, LPARAM lParam) {
    HWND control = reinterpret_cast<HWND>(lParam);
    if (nativeControlHasClass(control, L"Edit") || nativeControlHasClass(control, L"RICHEDIT50W")) {
        return std::nullopt;
    }
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetBkColor(dc, kFormBackgroundColor);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(formBackgroundBrush());
}

LRESULT NativeMainWindow::handleButtonColorMessage(WPARAM wParam) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetBkColor(dc, kFormBackgroundColor);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(formBackgroundBrush());
}

std::optional<LRESULT> NativeMainWindow::handleCommandMessage(WPARAM wParam) {
    const WORD commandId = LOWORD(wParam);
    if (commandId >= IDC_QUICK_SEND_BUTTON_BASE && commandId < IDC_QUICK_SEND_BUTTON_BASE + quickSendButtons_.size()) {
        if (HIWORD(wParam) == BN_CLICKED) {
            sendQuickPayload(static_cast<std::size_t>(commandId - IDC_QUICK_SEND_BUTTON_BASE));
        }
        return 0;
    }
    if (commandId >= IDC_QUICK_SEND_EDIT_BASE && commandId < IDC_QUICK_SEND_EDIT_BASE + quickSendEdits_.size()) {
        if (HIWORD(wParam) == EN_KILLFOCUS) {
            saveUiPreferences();
        }
        return 0;
    }

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
    case IDC_SEND_BUTTON:
        sendPayload();
        return 0;
    case IDC_CLEAR_BUTTON:
        clearLog();
        return 0;
    case IDC_DTR_CHECK:
    case IDC_RTS_CHECK:
        if (HIWORD(wParam) == BN_CLICKED) {
            applySerialLineControl(static_cast<WORD>(LOWORD(wParam)));
        }
        return 0;
    case IDC_FLOW_CONTROL_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            updateRtsControlState();
            saveUiPreferences();
            if ((!serialPort_.isOpen()
                    && selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None))
                        == static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts))
                || (serialPort_.isOpen() && serialPort_.usesHardwareRtsCts())) {
                setStatus(tx(T::RtsHardwareManaged));
            }
        }
        return 0;
    case IDC_LOG_FORMAT_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            rebuildLogView();
            saveUiPreferences();
            setStatus(tx(T::LogFormatChanged));
        }
        return 0;
    case IDC_LOG_ENCODING_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            rebuildLogView();
            saveUiPreferences();
            setStatus(tx(T::LogEncodingChanged));
        }
        return 0;
    case IDC_LOG_CACHE_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
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
        if (HIWORD(wParam) == CBN_SELCHANGE) {
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
    case IDC_SEND_MODE_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            saveUiPreferences();
            setSendModeStatus();
        }
        return 0;
    case IDC_TEXT_ENCODING_COMBO:
    case IDC_LINE_ENDING_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            saveUiPreferences();
        }
        return 0;
    case IDC_TIMED_SEND_CHECK:
        if (HIWORD(wParam) == BN_CLICKED) {
            sendControlState_.setTimedSendEnabled(SendMessageW(timedSendCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED);
            updateTimedSendTimer();
            saveUiPreferences();
            setStatus(sendControlState_.timedSendEnabled() ? tx(T::TimedSendEnabledStatus) : tx(T::TimedSendDisabledStatus));
        }
        return 0;
    case IDC_TIMED_PERIOD_EDIT:
        if (HIWORD(wParam) == EN_CHANGE && sendControlState_.timedSendEnabled()) {
            updateTimedSendTimer();
        } else if (HIWORD(wParam) == EN_KILLFOCUS) {
            saveUiPreferences();
        }
        return 0;
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
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            if (fileSend_.active()) {
                KillTimer(window_, IDT_FILE_SEND);
                SetTimer(window_, IDT_FILE_SEND, static_cast<UINT>(std::max<int>(1, static_cast<int>(selectedComboData(fileDelayCombo_, 0)))), nullptr);
            }
            saveUiPreferences();
        }
        return 0;
    case IDC_LOG_FILTER_EDIT:
        if (HIWORD(wParam) == EN_CHANGE) {
            KillTimer(window_, IDT_LOG_FILTER);
            SetTimer(window_, IDT_LOG_FILTER, 180, nullptr);
        }
        return 0;
    case IDC_LOG_SEARCH_EDIT:
        if (HIWORD(wParam) == EN_CHANGE) {
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
    case IDC_SAVE_PROFILE_BUTTON:
        saveCurrentSerialProfile();
        return 0;
    case IDC_PAUSE_SCROLL_BUTTON:
        toggleLogScrollPause();
        return 0;
    case IDC_HISTORY_COMBO:
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            applySelectedHistory();
        }
        return 0;
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
    case IDM_FILE_SAVE_PROFILE:
        saveCurrentSerialProfile();
        return 0;
    case IDM_FILE_EXIT:
        DestroyWindow(window_);
        return 0;
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
        setStatus(SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED ? tx(T::AutoReconnectEnabled) : tx(T::AutoReconnectDisabled));
        return 0;
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
    case IDM_TOOLS_FIND_LOG:
        findNextLogMatch();
        return 0;
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
    case IDM_HELP_ABOUT:
        showAbout();
        return 0;
    default:
        break;
    }

    return std::nullopt;
}

std::optional<LRESULT> NativeMainWindow::handleTimerMessage(WPARAM wParam) {
    if (wParam == IDT_SERIAL_POLL) {
        pollSerial();
        return 0;
    }
    if (wParam == IDT_TIMED_SEND) {
        sendPayload();
        return 0;
    }
    if (wParam == IDT_FILE_SEND) {
        pumpFileSend();
        return 0;
    }
    if (wParam == IDT_RECONNECT) {
        tryAutoReconnect();
        return 0;
    }
    if (wParam == IDT_LOG_FILTER) {
        KillTimer(window_, IDT_LOG_FILTER);
        updateLogFilter();
        return 0;
    }
    if (wParam == IDT_LOG_FLUSH) {
        KillTimer(window_, IDT_LOG_FLUSH);
        logFlushTimerActive_ = false;
        flushPendingLogEntries();
        return 0;
    }
    if (wParam == IDT_STATUS_CLOCK) {
        updateStatusSegments();
        return 0;
    }
    return std::nullopt;
}

LRESULT NativeMainWindow::handleDestroyMessage() {
    saveUiPreferences();
    for (UINT_PTR timerId : {
             IDT_SERIAL_POLL,
             IDT_RECONNECT,
             IDT_LOG_FILTER,
             IDT_TIMED_SEND,
             IDT_FILE_SEND,
             IDT_STATUS_CLOCK,
             IDT_LOG_FLUSH,
         }) {
        KillTimer(window_, timerId);
    }
    stopFileSend({});
    requestCancelModbusScan();
    closeModbusScanThread();
    releaseModbusScanOwnership();
    shutdownSerialPort();
    if (ownsUiFont_ && uiFont_ != nullptr) {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
        ownsUiFont_ = false;
    }
    if (richEditModule_ != nullptr) {
        FreeLibrary(richEditModule_);
        richEditModule_ = nullptr;
    }
    PostQuitMessage(0);
    return 0;
}

} // namespace svm::win32

#endif
