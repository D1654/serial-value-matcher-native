#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/resource.h"
#include "win32/utf8_win32.h"

#include <commctrl.h>
#include <memory>
#include <optional>

namespace svm::win32 {
namespace {

constexpr COLORREF kFormBackgroundColor = RGB(228, 228, 228);

HBRUSH formBackgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(kFormBackgroundColor);
    return brush;
}

} // namespace

LRESULT NativeMainWindow::handleCreateMessage() {
    const auto context = shellContext();
    createMenus();
    createControls();
    if (!store_.open(defaultStoreDirectory())) {
        reportStorageFailure(L"打开本地存储");
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
    SetTimer(context.window, IDT_SERIAL_POLL, 50, nullptr);
    SetTimer(context.window, IDT_STATUS_CLOCK, 1000, nullptr);
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
        scheduleLogFlushFrame();
        return 0;
    }
    if (wParam == IDT_UI_PREFERENCES_SAVE) {
        KillTimer(window_, IDT_UI_PREFERENCES_SAVE);
        saveUiPreferences();
        return 0;
    }
    if (wParam == IDT_STATUS_CLOCK) {
        if (modbusScanRunning_.load(std::memory_order_relaxed)
            && modbusScanTerminalResult_.load(std::memory_order_acquire) != nullptr) {
            handleModbusScanDone();
        }
        scheduleStatusFrame();
        return 0;
    }
    return std::nullopt;
}

LRESULT NativeMainWindow::handleDestroyMessage() {
    shuttingDown_ = true;
    const auto context = shellContext();
    saveUiPreferences();
    for (const UINT_PTR timerId : kNativeMainWindowShellTimerIds) {
        KillTimer(context.window, timerId);
    }
    stopFileSend({}, false);
    requestCancelModbusScan();
    const NativeModbusThreadCloseResult closeResult = closeModbusScanThread();
    if (closeResult == NativeModbusThreadCloseResult::NotJoined) {
        ExitProcess(1);
    }
    settlePendingModbusScanMessages();
    std::unique_ptr<NativeModbusScanResult> terminalResult(
        modbusScanTerminalResult_.exchange(nullptr, std::memory_order_acq_rel));
    if (terminalResult) {
        if (!store_.isOpen() || !store_.saveScanExecution(terminalResult->execution)) {
            reportStorageFailure(L"保存最终 Modbus 扫描结果");
        }
    }
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
