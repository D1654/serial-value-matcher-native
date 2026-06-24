#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_candidate_cache_state.h"
#include "win32/native_control_utils.h"
#include "win32/native_layout_metrics.h"
#include "win32/native_log_view.h"
#include "win32/native_log_scroll_state.h"
#include "win32/native_modbus_scan_request.h"
#include "win32/native_modbus_scan_ui_state.h"
#include "win32/native_progress_control.h"
#include "win32/native_send_codec.h"
#include "win32/native_send_control_state.h"
#include "win32/native_send_history_state.h"
#include "win32/native_status_counters_state.h"
#include "win32/native_ui_preferences.h"
#include "win32/native_workbench_tab_state.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_types.h"

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <richedit.h>
#include <memory>
#include <string_view>
#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

constexpr std::size_t kMaxRenderedLogLineChars = 4096;
constexpr COLORREF kFormBackgroundColor = RGB(228, 228, 228);

const wchar_t* tx(T id) {
    return uiText(id);
}

HBRUSH formBackgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(kFormBackgroundColor);
    return brush;
}

HFONT createSystemUiFont() {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0) != FALSE) {
        return CreateFontIndirectW(&metrics.lfMessageFont);
    }

    return nullptr;
}

HWND createSingleLineEdit(HWND parent, HINSTANCE instance, int controlId, const wchar_t* text, DWORD extraStyle = 0) {
    return CreateWindowExW(
        0,
        L"EDIT",
        text,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | extraStyle,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        instance,
        nullptr);
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

LRESULT NativeMainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
        minMax->ptMinTrackSize.x = kMinTrackWidth;
        minMax->ptMinTrackSize.y = kMinTrackHeight;
        return 0;
    }
    case WM_CREATE:
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
    case WM_SIZE:
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_NOTIFY: {
        const auto* notification = reinterpret_cast<NMHDR*>(lParam);
        if (notification != nullptr && notification->idFrom == IDC_WORK_TABS && notification->code == TCN_SELCHANGE) {
            updateWorkbenchTab();
            return 0;
        }
        break;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CTLCOLORSTATIC: {
        HWND control = reinterpret_cast<HWND>(lParam);
        if (nativeControlHasClass(control, L"Edit") || nativeControlHasClass(control, L"RICHEDIT50W")) {
            break;
        }
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, kFormBackgroundColor);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(formBackgroundBrush());
    }
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, kFormBackgroundColor);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(formBackgroundBrush());
    }
    case WM_COMMAND: {
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
        break;
    }
    case WM_TIMER:
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
        break;
    case kNativeModbusScanDoneMessage:
        handleModbusScanDone(reinterpret_cast<NativeModbusScanResult*>(lParam));
        return 0;
    case kNativeModbusScanProgressMessage:
        handleModbusScanProgress(reinterpret_cast<NativeModbusScanProgress*>(lParam));
        return 0;
    case kNativeModbusScanDataMessage:
        handleModbusScanDataBatch(reinterpret_cast<NativeModbusScanDataBatch*>(lParam));
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
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
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void NativeMainWindow::createControls() {
    uiFont_ = createSystemUiFont();
    ownsUiFont_ = uiFont_ != nullptr;
    if (uiFont_ == nullptr) {
        uiFont_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    richEditModule_ = LoadLibraryW(L"Msftedit.dll");
    receiveLogUsesRichEdit_ = richEditModule_ != nullptr;

    connectionGroup_ = CreateWindowExW(0, L"BUTTON", tx(T::ConnectionGroup), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    sendGroup_ = CreateWindowExW(0, L"BUTTON", tx(T::SendGroup), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    workflowGroup_ = CreateWindowExW(0, L"BUTTON", tx(T::WorkflowGroup), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logGroup_ = CreateWindowExW(0, L"BUTTON", tx(T::LogGroup), WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    serialPanelTitle_ = CreateWindowExW(0, L"STATIC", tx(T::ConnectionGroup), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logPanelTitle_ = CreateWindowExW(0, L"STATIC", tx(T::LogGroup), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    workPanelTitle_ = CreateWindowExW(0, L"STATIC", tx(T::SendGroup), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    workTabs_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_WORK_TABS), instance_, nullptr);
    workPageBackground_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    workflowHint_ = CreateWindowExW(0, L"STATIC", tx(T::WorkflowHint), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    portLabel_ = CreateWindowExW(0, L"STATIC", tx(T::PortLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    portCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PORT_COMBO), instance_, nullptr);
    refreshButton_ = CreateWindowExW(0, L"BUTTON", tx(T::RefreshButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_REFRESH_BUTTON), instance_, nullptr);
    saveProfileButton_ = CreateWindowExW(0, L"BUTTON", tx(T::SaveProfileButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SAVE_PROFILE_BUTTON), instance_, nullptr);
    baudLabel_ = CreateWindowExW(0, L"STATIC", tx(T::BaudLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    baudCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_BAUD_EDIT), instance_, nullptr);
    dataBitsLabel_ = CreateWindowExW(0, L"STATIC", tx(T::DataBitsLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    dataBitsCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DATA_BITS_COMBO), instance_, nullptr);
    parityLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ParityLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    parityCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PARITY_COMBO), instance_, nullptr);
    stopBitsLabel_ = CreateWindowExW(0, L"STATIC", tx(T::StopBitsLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    stopBitsCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_STOP_BITS_COMBO), instance_, nullptr);
    flowControlLabel_ = CreateWindowExW(0, L"STATIC", tx(T::FlowControlLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    flowControlCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_FLOW_CONTROL_COMBO), instance_, nullptr);
    dtrCheck_ = CreateWindowExW(0, L"BUTTON", L"DTR", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DTR_CHECK), instance_, nullptr);
    rtsCheck_ = CreateWindowExW(0, L"BUTTON", L"RTS", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RTS_CHECK), instance_, nullptr);
    autoReconnectCheck_ = CreateWindowExW(0, L"BUTTON", tx(T::AutoReconnectCheck), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_AUTO_RECONNECT_CHECK), instance_, nullptr);
    connectButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ConnectButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CONNECT_BUTTON), instance_, nullptr);
    disconnectButton_ = CreateWindowExW(0, L"BUTTON", tx(T::DisconnectButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DISCONNECT_BUTTON), instance_, nullptr);
    sendModeLabel_ = CreateWindowExW(0, L"STATIC", tx(T::SendModeLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    sendModeCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_MODE_COMBO), instance_, nullptr);
    sendEncodingLabel_ = CreateWindowExW(0, L"STATIC", tx(T::SendEncodingLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    textEncodingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TEXT_ENCODING_COMBO), instance_, nullptr);
    lineEndingLabel_ = CreateWindowExW(0, L"STATIC", tx(T::LineEndingLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    lineEndingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LINE_ENDING_COMBO), instance_, nullptr);
    sendHistoryLabel_ = CreateWindowExW(0, L"STATIC", tx(T::SendHistoryLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logFormatLabel_ = CreateWindowExW(0, L"STATIC", tx(T::LogFormatLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logFormatCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_FORMAT_COMBO), instance_, nullptr);
    logEncodingLabel_ = CreateWindowExW(0, L"STATIC", tx(T::LogEncodingLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logEncodingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_ENCODING_COMBO), instance_, nullptr);
    copyLogButton_ = CreateWindowExW(0, L"BUTTON", tx(T::CopyLogButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_COPY_LOG_BUTTON), instance_, nullptr);
    exportLogButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ExportLogButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_EXPORT_LOG_BUTTON), instance_, nullptr);
    logFilterLabel_ = CreateWindowExW(0, L"STATIC", tx(T::LogFilterLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logFilterEdit_ = createSingleLineEdit(window_, instance_, IDC_LOG_FILTER_EDIT, L"");
    logSearchLabel_ = CreateWindowExW(0, L"STATIC", tx(T::LogSearchLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logSearchEdit_ = createSingleLineEdit(window_, instance_, IDC_LOG_SEARCH_EDIT, L"");
    findLogButton_ = CreateWindowExW(0, L"BUTTON", tx(T::FindButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_FIND_BUTTON), instance_, nullptr);
    historyCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_HISTORY_COMBO), instance_, nullptr);
    sendEdit_ = createSingleLineEdit(window_, instance_, IDC_SEND_EDIT, L"");
    sendButton_ = CreateWindowExW(0, L"BUTTON", tx(T::SendButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_BUTTON), instance_, nullptr);
    timedSendCheck_ = CreateWindowExW(0, L"BUTTON", tx(T::TimedSendCheck), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TIMED_SEND_CHECK), instance_, nullptr);
    timedPeriodLabel_ = CreateWindowExW(0, L"STATIC", tx(T::TimedPeriodLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    timedPeriodEdit_ = createSingleLineEdit(window_, instance_, IDC_TIMED_PERIOD_EDIT, L"1000", ES_NUMBER);
    for (std::size_t index = 0; index < quickSendEdits_.size(); ++index) {
        quickSendEdits_[index] = createSingleLineEdit(
            window_,
            instance_,
            static_cast<int>(IDC_QUICK_SEND_EDIT_BASE + index),
            L"");
        const std::wstring quickButtonText = std::wstring(L"\u53D1") + std::to_wstring(index + 1);
        quickSendButtons_[index] = CreateWindowExW(
            0,
            L"BUTTON",
            quickButtonText.c_str(),
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(IDC_QUICK_SEND_BUTTON_BASE + index),
            instance_,
            nullptr);
    }
    filePathLabel_ = CreateWindowExW(0, L"STATIC", tx(T::FilePathLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    filePathEdit_ = createSingleLineEdit(window_, instance_, IDC_FILE_PATH_EDIT, L"");
    fileBrowseButton_ = CreateWindowExW(0, L"BUTTON", tx(T::FileBrowseButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_FILE_BROWSE_BUTTON), instance_, nullptr);
    fileSendButton_ = CreateWindowExW(0, L"BUTTON", tx(T::FileSendButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_FILE_SEND_BUTTON), instance_, nullptr);
    fileStopButton_ = CreateWindowExW(0, L"BUTTON", tx(T::FileStopButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_FILE_STOP_BUTTON), instance_, nullptr);
    fileDelayLabel_ = CreateWindowExW(0, L"STATIC", tx(T::FileDelayLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    fileDelayCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_FILE_DELAY_COMBO), instance_, nullptr);
    fileProgressLabel_ = CreateWindowExW(0, L"STATIC", tx(T::FileProgressLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    fileProgress_ = createNativeProgressControl(window_, instance_, IDC_FILE_PROGRESS);
    logCacheLabel_ = CreateWindowExW(0, L"STATIC", tx(T::LogCacheLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    logCacheCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_CACHE_COMBO), instance_, nullptr);
    rawEventRetentionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::RawEventRetentionLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    rawEventRetentionCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RAW_EVENT_RETENTION_COMBO), instance_, nullptr);
    sideActionSeparator_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    pauseScrollButton_ = CreateWindowExW(0, L"BUTTON", tx(T::PauseScrollButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PAUSE_SCROLL_BUTTON), instance_, nullptr);
    sideHelpSeparator_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    sideHelpFrame_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDFRAME, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    sideHelpTitle_ = CreateWindowExW(0, L"STATIC", tx(T::SideHelpTitle), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    sideHelpText_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    clearButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ClearButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CLEAR_BUTTON), instance_, nullptr);
    modbusButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ModbusScanButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_MODBUS_BUTTON), instance_, nullptr);
    analysisButton_ = CreateWindowExW(0, L"BUTTON", tx(T::AnalysisButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_ANALYSIS_BUTTON), instance_, nullptr);
    ruleVerifyButton_ = CreateWindowExW(0, L"BUTTON", tx(T::RuleVerifyButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RULE_VERIFY_BUTTON), instance_, nullptr);
    exportReportButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ExportReportButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_EXPORT_REPORT_BUTTON), instance_, nullptr);
    scanSectionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanSectionLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanSlaveLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanSlaveLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanSlaveEdit_ = createSingleLineEdit(window_, instance_, IDC_SCAN_SLAVE_EDIT, L"1", ES_NUMBER);
    scanFunctionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanFunctionLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanFunctionCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_FUNCTION_COMBO), instance_, nullptr);
    scanStartLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanStartLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanStartEdit_ = createSingleLineEdit(window_, instance_, IDC_SCAN_START_EDIT, L"0", ES_NUMBER);
    scanEndLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanEndLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanEndEdit_ = createSingleLineEdit(window_, instance_, IDC_SCAN_END_EDIT, L"15", ES_NUMBER);
    modbusProgressLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ModbusProgressLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    modbusProgress_ = createNativeProgressControl(window_, instance_, IDC_MODBUS_PROGRESS);
    modbusProgressText_ = CreateWindowExW(0, L"STATIC", L"0/0", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    analysisSectionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::AnalysisSectionLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetStatic_ = CreateWindowExW(0, L"STATIC", tx(T::TargetNameLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetLabelEdit_ = createSingleLineEdit(window_, instance_, IDC_TARGET_LABEL_EDIT, tx(T::TargetNameDefault));
    targetValueStatic_ = CreateWindowExW(0, L"STATIC", tx(T::TargetValueLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetValueEdit_ = createSingleLineEdit(window_, instance_, IDC_TARGET_VALUE_EDIT, tx(T::TargetValueDefault));
    targetUnitStatic_ = CreateWindowExW(0, L"STATIC", tx(T::TargetUnitLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetUnitEdit_ = createSingleLineEdit(window_, instance_, IDC_TARGET_UNIT_EDIT, tx(T::TargetUnitDefault));
    toleranceStatic_ = CreateWindowExW(0, L"STATIC", tx(T::ToleranceFieldLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    toleranceEdit_ = createSingleLineEdit(window_, instance_, IDC_TOLERANCE_EDIT, tx(T::ToleranceDefault));
    candidateStatic_ = CreateWindowExW(0, L"STATIC", tx(T::CandidateLabel), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    candidateCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CANDIDATE_COMBO), instance_, nullptr);
    receiveLog_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        receiveLogUsesRichEdit_ ? MSFTEDIT_CLASS : L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | ES_NOHIDESEL,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(IDC_RECEIVE_LOG),
        instance_,
        nullptr);
    if (receiveLog_ == nullptr && receiveLogUsesRichEdit_) {
        receiveLogUsesRichEdit_ = false;
        FreeLibrary(richEditModule_);
        richEditModule_ = nullptr;
        receiveLog_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(IDC_RECEIVE_LOG),
            instance_,
            nullptr);
    }
    statusText_ = CreateWindowExW(0, L"STATIC", tx(T::InitialStatus), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_STATUS_TEXT), instance_, nullptr);
    txStatusText_ = CreateWindowExW(0, L"STATIC", L"TX 0 B", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    rxStatusText_ = CreateWindowExW(0, L"STATIC", L"RX 0 B", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    clockStatusText_ = CreateWindowExW(0, L"STATIC", L"--:--:--", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    SendMessageW(logFilterEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::LogFilterCue)));
    SendMessageW(logSearchEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::LogSearchCue)));
    SendMessageW(targetLabelEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::TargetNameCue)));
    SendMessageW(targetValueEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::TargetValueCue)));
    SendMessageW(targetUnitEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::TargetUnitCue)));
    SendMessageW(toleranceEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::ToleranceCue)));
    nativeLogSetTextLimit(receiveLog_, logVisibleCharLimit_ + kMaxRenderedLogLineChars);

    for (const wchar_t* label : {tx(T::WorkbenchTabSingle), tx(T::WorkbenchTabQuick), tx(T::WorkbenchTabFile), tx(T::WorkbenchTabScan), tx(T::WorkbenchTabSettings)}) {
        TCITEMW tab = {};
        tab.mask = TCIF_TEXT;
        tab.pszText = const_cast<wchar_t*>(label);
        TabCtrl_InsertItem(workTabs_, TabCtrl_GetItemCount(workTabs_), &tab);
    }

    populateSerialOptionControls();
    setDefaultFonts();
    applyLogTheme(0);
    updateRtsControlState();
    enableControl(fileStopButton_, false);
    updateFileSendProgress();
    updateModbusScanProgress(0, 0, 0, 0, 0);
    updateStatusSegments();
    updateWorkbenchTab();
}

void NativeMainWindow::populateSerialOptionControls() {
    for (int baud : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}) {
        const std::wstring text = std::to_wstring(baud);
        addComboItem(baudCombo_, text.c_str(), baud);
    }
    selectComboData(baudCombo_, 115200);

    for (int bits : {8, 7, 6, 5}) {
        const std::wstring text = std::to_wstring(bits);
        addComboItem(dataBitsCombo_, text.c_str(), bits);
    }
    selectComboData(dataBitsCombo_, 8);

    addComboItem(parityCombo_, tx(T::NoParity), static_cast<LPARAM>(SerialParity::None));
    addComboItem(parityCombo_, tx(T::OddParity), static_cast<LPARAM>(SerialParity::Odd));
    addComboItem(parityCombo_, tx(T::EvenParity), static_cast<LPARAM>(SerialParity::Even));
    addComboItem(parityCombo_, tx(T::MarkParity), static_cast<LPARAM>(SerialParity::Mark));
    addComboItem(parityCombo_, tx(T::SpaceParity), static_cast<LPARAM>(SerialParity::Space));
    selectComboData(parityCombo_, static_cast<LPARAM>(SerialParity::None));

    addComboItem(stopBitsCombo_, L"1", static_cast<LPARAM>(SerialStopBits::One));
    addComboItem(stopBitsCombo_, L"1.5", static_cast<LPARAM>(SerialStopBits::OnePointFive));
    addComboItem(stopBitsCombo_, L"2", static_cast<LPARAM>(SerialStopBits::Two));
    selectComboData(stopBitsCombo_, static_cast<LPARAM>(SerialStopBits::One));

    addComboItem(flowControlCombo_, tx(T::NoFlowControl), static_cast<LPARAM>(SerialFlowControl::None));
    addComboItem(flowControlCombo_, L"RTS/CTS", static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts));
    addComboItem(flowControlCombo_, L"XON/XOFF", static_cast<LPARAM>(SerialFlowControl::SoftwareXonXoff));
    selectComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None));

    addComboItem(sendModeCombo_, tx(T::TextMode), 0);
    addComboItem(sendModeCombo_, tx(T::HexByteStreamMode), 1);
    addComboItem(sendModeCombo_, tx(T::DecimalByteStreamMode), 2);
    addComboItem(sendModeCombo_, tx(T::BinaryByteStreamMode), 3);
    selectComboData(sendModeCombo_, 0);

    addComboItem(textEncodingCombo_, tx(T::Utf8Encoding), CP_UTF8);
    addComboItem(textEncodingCombo_, tx(T::GbkEncoding), kNativeCodePageGbk);
    addComboItem(textEncodingCombo_, tx(T::AnsiEncoding), CP_ACP);
    addComboItem(textEncodingCombo_, tx(T::AsciiEncoding), kNativeCodePageAscii);
    selectComboData(textEncodingCombo_, CP_UTF8);

    addComboItem(lineEndingCombo_, tx(T::NoLineEnding), 0);
    addComboItem(lineEndingCombo_, L"CR", 1);
    addComboItem(lineEndingCombo_, L"LF", 2);
    addComboItem(lineEndingCombo_, L"CRLF", 3);
    selectComboData(lineEndingCombo_, 0);

    addComboItem(logFormatCombo_, tx(T::LogFormatHex), 0);
    addComboItem(logFormatCombo_, tx(T::LogFormatDecimal), 1);
    addComboItem(logFormatCombo_, tx(T::LogFormatBinary), 2);
    addComboItem(logFormatCombo_, tx(T::LogFormatText), 3);
    addComboItem(logFormatCombo_, tx(T::LogFormatHexText), 4);
    selectComboData(logFormatCombo_, 0);

    addComboItem(logEncodingCombo_, tx(T::Utf8Encoding), CP_UTF8);
    addComboItem(logEncodingCombo_, tx(T::GbkEncoding), kNativeCodePageGbk);
    addComboItem(logEncodingCombo_, tx(T::AnsiEncoding), CP_ACP);
    addComboItem(logEncodingCombo_, tx(T::AsciiEncoding), kNativeCodePageAscii);
    selectComboData(logEncodingCombo_, CP_UTF8);

    addComboItem(logCacheCombo_, L"200K", 200000);
    addComboItem(logCacheCombo_, L"350K", 350000);
    addComboItem(logCacheCombo_, L"500K", 500000);
    addComboItem(logCacheCombo_, L"1M", 1000000);
    addComboItem(logCacheCombo_, L"2M", 2000000);
    addComboItem(logCacheCombo_, L"5M", 5000000);
    addComboItem(logCacheCombo_, L"10M", 10000000);
    addComboItem(logCacheCombo_, L"20M", 20000000);
    addComboItem(logCacheCombo_, L"50M", 50000000);
    addComboItem(logCacheCombo_, L"100M", 100000000);
    selectComboData(logCacheCombo_, static_cast<LPARAM>(kNativeDefaultLogVisibleChars));

    addComboItem(rawEventRetentionCombo_, L"100M", 100);
    addComboItem(rawEventRetentionCombo_, L"500M", 500);
    addComboItem(rawEventRetentionCombo_, L"1000M", 1000);
    addComboItem(rawEventRetentionCombo_, L"\u4E0D\u9650\u5236", 0);
    selectComboData(rawEventRetentionCombo_, kNativeDefaultRawEventRetentionMb);

    addComboItem(fileDelayCombo_, L"0 ms", 0);
    addComboItem(fileDelayCombo_, L"1 ms", 1);
    addComboItem(fileDelayCombo_, L"5 ms", 5);
    addComboItem(fileDelayCombo_, L"10 ms", 10);
    addComboItem(fileDelayCombo_, L"20 ms", 20);
    addComboItem(fileDelayCombo_, L"50 ms", 50);
    selectComboData(fileDelayCombo_, 0);

    addComboItem(scanFunctionCombo_, tx(T::Fc03Holding), 3);
    addComboItem(scanFunctionCombo_, tx(T::Fc04Input), 4);
    selectComboData(scanFunctionCombo_, 3);

    addComboItem(candidateCombo_, tx(T::CandidatePlaceholder), 0);
    selectComboData(candidateCombo_, 0);
}

} // namespace svm::win32

#endif
