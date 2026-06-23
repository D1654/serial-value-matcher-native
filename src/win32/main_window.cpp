#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_candidate_cache_state.h"
#include "win32/native_control_utils.h"
#include "win32/native_layout_metrics.h"
#include "win32/native_analysis_workflow.h"
#include "win32/native_log_view.h"
#include "win32/native_log_scroll_state.h"
#include "win32/native_modbus_scan_request.h"
#include "win32/native_modbus_scan_ui_state.h"
#include "win32/native_reconnect_state.h"
#include "win32/native_send_codec.h"
#include "win32/native_send_control_state.h"
#include "win32/native_send_history_state.h"
#include "win32/native_status_counters_state.h"
#include "win32/native_time_utils.h"
#include "win32/native_ui_preferences.h"
#include "win32/native_workbench_tab_state.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_enumerator.h"
#include "win32/win32_serial_types.h"

#include "core/modbus_core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

namespace modbus_core = ::svm::core::modbus;

constexpr wchar_t kWindowClassName[] = L"SvmNativeMainWindow";
constexpr std::size_t kMaxRenderedLogLineChars = 4096;
constexpr COLORREF kFormBackgroundColor = RGB(228, 228, 228);
constexpr wchar_t kNativeProgressClassName[] = L"SvmNativeProgress";
constexpr COLORREF kNativeProgressBorderColor = RGB(96, 96, 96);
constexpr COLORREF kNativeProgressBackgroundColor = RGB(255, 255, 255);
constexpr COLORREF kNativeProgressFillColor = RGB(0, 120, 215);

const wchar_t* tx(T id) {
    return uiText(id);
}

struct NativeProgressState {
    int minimum = 0;
    int maximum = 1000;
    int position = 0;
};

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

int clampNativeProgressPosition(const NativeProgressState& state, int position) {
    return std::clamp(position, state.minimum, state.maximum);
}

void paintNativeProgress(HWND window, HDC dc) {
    RECT rect = {};
    GetClientRect(window, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    auto* state = reinterpret_cast<NativeProgressState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    NativeProgressState fallbackState;
    if (state == nullptr) {
        state = &fallbackState;
    }

    HBRUSH borderBrush = CreateSolidBrush(kNativeProgressBorderColor);
    FillRect(dc, &rect, borderBrush);
    DeleteObject(borderBrush);

    RECT innerRect = {
        rect.left + 2,
        rect.top + 2,
        std::max<LONG>(rect.left + 2, rect.right - 2),
        std::max<LONG>(rect.top + 2, rect.bottom - 2),
    };
    HBRUSH backgroundBrush = CreateSolidBrush(kNativeProgressBackgroundColor);
    FillRect(dc, &innerRect, backgroundBrush);
    DeleteObject(backgroundBrush);

    const int innerWidth = std::max(0, static_cast<int>(innerRect.right - innerRect.left));
    const int range = std::max(1, state->maximum - state->minimum);
    const int fillWidth = std::clamp(
        ((clampNativeProgressPosition(*state, state->position) - state->minimum) * innerWidth) / range,
        0,
        innerWidth);
    if (fillWidth > 0) {
        RECT fillRect = {innerRect.left, innerRect.top, innerRect.left + fillWidth, innerRect.bottom};
        HBRUSH fillBrush = CreateSolidBrush(kNativeProgressFillColor);
        FillRect(dc, &fillRect, fillBrush);
        DeleteObject(fillBrush);
    }
}

LRESULT CALLBACK nativeProgressWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<NativeProgressState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new NativeProgressState()));
        return TRUE;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        paintNativeProgress(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case PBM_SETRANGE32:
        if (state != nullptr) {
            const int oldMinimum = state->minimum;
            const int oldMaximum = state->maximum;
            const int oldPosition = state->position;
            state->minimum = static_cast<int>(wParam);
            state->maximum = static_cast<int>(lParam);
            if (state->maximum <= state->minimum) {
                state->maximum = state->minimum + 1;
            }
            state->position = clampNativeProgressPosition(*state, state->position);
            if (state->minimum != oldMinimum || state->maximum != oldMaximum || state->position != oldPosition) {
                InvalidateRect(window, nullptr, TRUE);
            }
        }
        return 0;
    case PBM_SETPOS:
        if (state != nullptr) {
            const int previousPosition = state->position;
            state->position = clampNativeProgressPosition(*state, static_cast<int>(wParam));
            if (state->position != previousPosition) {
                InvalidateRect(window, nullptr, TRUE);
            }
            return previousPosition;
        }
        return 0;
    case PBM_GETPOS:
        return state != nullptr ? state->position : 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool registerNativeProgressClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = nativeProgressWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kNativeProgressClassName;
    if (RegisterClassExW(&windowClass) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool nativeProgressStyleHasVisibleFrame() {
    return kNativeProgressBorderColor != kFormBackgroundColor
        && kNativeProgressBorderColor != kNativeProgressBackgroundColor
        && kNativeProgressClassName[0] != L'\0';
}

HWND createProgressControl(HWND parent, HINSTANCE instance, int controlId) {
    if (!registerNativeProgressClass(instance)) {
        return nullptr;
    }
    return CreateWindowExW(
        0,
        kNativeProgressClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
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

void writeSelfTestTrace(const char* message) {
    char path[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("SVM_NATIVE_SELF_TEST_LOG", path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    std::ofstream output(path, std::ios::app);
    if (output) {
        output << message << '\n';
    }
}

} // namespace

bool NativeMainWindow::create(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = NativeMainWindow::windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass)) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        tx(T::WindowTitle),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1220,
        780,
        nullptr,
        nullptr,
        instance_,
        this);
    return window_ != nullptr;
}

void NativeMainWindow::show(int commandShow) {
    ShowWindow(window_, commandShow);
    UpdateWindow(window_);
}

int NativeMainWindow::runMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool NativeMainWindow::runSelfTest() {
    const auto fail = [](const char* step) {
        writeSelfTestTrace(step);
        return false;
    };
    const std::wstring expectedChinese = {
        static_cast<wchar_t>(0x4E32),
        static_cast<wchar_t>(0x53E3),
        static_cast<wchar_t>(0x503C),
        static_cast<wchar_t>(0x5339),
        static_cast<wchar_t>(0x914D),
        static_cast<wchar_t>(0x5668),
    };
    constexpr char kExpectedUtf8[] = "\xE4\xB8\xB2\xE5\x8F\xA3\xE5\x80\xBC\xE5\x8C\xB9\xE9\x85\x8D\xE5\x99\xA8";
    if (std::wstring(tx(T::SelfTestText)) != expectedChinese
        || utf8ToWide(kExpectedUtf8) != expectedChinese
        || wideToUtf8(expectedChinese) != kExpectedUtf8) {
        return fail("unicode-roundtrip");
    }

    SerialOpenOptions options;
    options.portName = "COM1";
    if (!validateSerialOpenOptions(options).ok) {
        return fail("serial-options");
    }
    if (makeWin32DevicePath("COM10") != R"(\\.\COM10)") {
        return fail("serial-device-path");
    }
    if (!logToolbarLayoutIsSane(360)) {
        return fail("log-toolbar-360");
    }
    if (!logToolbarLayoutIsSane(554)) {
        return fail("log-toolbar-554");
    }
    if (!sendControlLayoutIsSane(320)) {
        return fail("send-layout-320");
    }
    if (!sendControlLayoutIsSane(428)) {
        return fail("send-layout-428");
    }
    if (!scanTabLayoutIsSane(554, 132)) {
        return fail("scan-tab-layout-554x132");
    }
    if (!nativeProgressStyleHasVisibleFrame()) {
        return fail("progress-border-style");
    }
    if (!mainLayoutProbeIsFullyUsableAtSize(kMinTrackWidth, kMinTrackHeight)) {
        return fail("main-layout-min");
    }
    if (!mainLayoutProbeIsFullyUsableAtSize(1040, 720)) {
        return fail("main-layout-1040x720");
    }
    if (!mainLayoutProbeIsFullyUsableAtSize(1366, 768)) {
        return fail("main-layout-1366x768");
    }
    if (!mainLayoutProbeIsStableAtSize(640, 400)) {
        return fail("main-layout-640x400");
    }
    if (!mainLayoutProbeIsStableAtSize(480, 320)) {
        return fail("main-layout-480x320");
    }
    if (!mainLayoutProbeIsStableAtSize(320, 240)) {
        return fail("main-layout-320x240");
    }
    if (!mainLayoutProbeIsStableAtSize(1, 1)) {
        return fail("main-layout-1x1");
    }

    wchar_t tempPathBuffer[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPathBuffer) == 0) {
        return fail("temp-path");
    }

    const auto storePath = std::filesystem::path(tempPathBuffer) / L"svm-native-win32-self-test";
    std::filesystem::remove_all(storePath);
    native_storage::NativeSessionStore store;
    if (!store.open(storePath)) {
        return fail("store-open");
    }

    native_storage::RawIoEvent event;
    event.sessionId = "self-test";
    event.direction = "Tx";
    event.timestampUtc = nativeUtcTimestampText();
    event.endpoint = "COM1";
    event.payload = {0x01, 0x03, 0x00, 0x00};

    native_storage::SerialProfile profile;
    profile.portName = "COM1";
    profile.baudRate = 115200;
    profile.dataBits = 8;
    profile.parity = "None";
    profile.stopBits = "One";
    profile.flowControl = "None";
    profile.updatedAtUtc = nativeUtcTimestampText();

    native_storage::SendHistoryEntry history;
    history.content = "AT+\xE6\xB5\x8B\xE8\xAF\x95";
    history.payloadMode = 0;
    history.lineEnding = 0;
    history.textEncodingCodePage = CP_UTF8;
    history.sentAtUtc = nativeUtcTimestampText();

    native_storage::UiPreferences preferences;
    preferences.logThemeIndex = 1;
    preferences.logFormat = 4;
    preferences.logEncodingCodePage = CP_UTF8;
    preferences.showLogTimestamps = false;
    preferences.updatedAtUtc = nativeUtcTimestampText();

    bool ok = true;
    const auto storageFail = [&](const char* step) {
        ok = false;
        writeSelfTestTrace(step);
    };
    if (!store.appendRawEvent(event)) {
        storageFail("storage-append-raw-event");
    } else if (store.rawEventCount() != 1) {
        storageFail("storage-raw-event-count");
    } else if (!store.saveSerialProfile(profile)) {
        storageFail("storage-save-profile");
    } else if (!store.latestSerialProfile().has_value()) {
        storageFail("storage-read-profile");
    } else if (!store.saveSendHistory(history)) {
        storageFail("storage-save-history");
    } else if (store.recentSendHistory(1).empty()) {
        storageFail("storage-read-history");
    } else if (!store.saveUiPreferences(preferences)) {
        storageFail("storage-save-preferences");
    } else if (!store.latestUiPreferences().has_value()) {
        storageFail("storage-read-preferences");
    }
    std::filesystem::remove_all(storePath);
    if (!ok) {
        return false;
    }
    writeSelfTestTrace("ok");
    return ok;
}

bool NativeMainWindow::runUiPerformanceTest() {
    const auto fail = [](const char* step) {
        writeSelfTestTrace(step);
        return false;
    };

    NativeMainWindow window;
    if (!window.create(GetModuleHandleW(nullptr))) {
        return fail("ui-perf-create-window");
    }

    ShowWindow(window.window_, SW_SHOWNOACTIVATE);
    UpdateWindow(window.window_);
    window.layoutControls(kMinTrackWidth, kMinTrackHeight);

    constexpr int kIterations = 300;
    constexpr auto kMaxElapsed = std::chrono::milliseconds(12000);
    const std::uint64_t baselineLayoutPasses = window.layoutPassCount_;
    const std::uint64_t baselineLayoutRevision = window.workbenchTabState_.layoutRevision();
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < kIterations; ++index) {
        const int tabIndex = index % 5;
        TabCtrl_SetCurSel(window.workTabs_, tabIndex);
        NMHDR notification = {};
        notification.hwndFrom = window.workTabs_;
        notification.idFrom = IDC_WORK_TABS;
        notification.code = TCN_SELCHANGE;
        SendMessageW(window.window_, WM_NOTIFY, IDC_WORK_TABS, reinterpret_cast<LPARAM>(&notification));
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    bool ok = true;
    if (window.layoutPassCount_ != baselineLayoutPasses) {
        writeSelfTestTrace("ui-perf-layout-regression");
        ok = false;
    }
    if (window.workbenchTabState_.layoutRevision() != baselineLayoutRevision) {
        writeSelfTestTrace("ui-perf-revision-regression");
        ok = false;
    }
    if (window.workbenchTabState_.applyCount() < static_cast<std::uint64_t>(kIterations)) {
        writeSelfTestTrace("ui-perf-apply-count");
        ok = false;
    }
    if (elapsedMs > kMaxElapsed) {
        writeSelfTestTrace("ui-perf-too-slow");
        ok = false;
    }

    window.clearLog();
    constexpr int kLogIterations = 1200;
    constexpr auto kMaxLogElapsed = std::chrono::milliseconds(12000);
    const auto logStart = std::chrono::steady_clock::now();
    for (int index = 0; index < kLogIterations; ++index) {
        window.appendLog(NativeLogKind::Rx, L"[RX] 01 03 02 00 2A 39 84");
    }
    window.flushPendingLogEntries();
    const auto logElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - logStart);
    if (!window.pendingLogLines_.empty()) {
        writeSelfTestTrace("ui-perf-log-pending");
        ok = false;
    }
    if (window.visibleLogLineCount_ != static_cast<std::size_t>(kLogIterations)) {
        writeSelfTestTrace("ui-perf-log-line-count");
        ok = false;
    }
    if (window.logFlushPassCount_ == 0) {
        writeSelfTestTrace("ui-perf-log-no-flush");
        ok = false;
    }
    if (logElapsedMs > kMaxLogElapsed) {
        writeSelfTestTrace("ui-perf-log-too-slow");
        ok = false;
    }

    window.clearLog();
    const std::uint64_t trimRebuildBaseline = window.logRebuildPassCount_;
    const std::size_t trimStressLines = window.logEntryLimit_ + 800;
    for (std::size_t index = 0; index < trimStressLines; ++index) {
        window.appendLog(NativeLogKind::Rx, L"[RX] 01");
    }
    window.flushPendingLogEntries();
    const std::uint64_t trimRebuildCount = window.logRebuildPassCount_ - trimRebuildBaseline;
    if (window.logEntries_.size() > window.logEntryLimit_) {
        writeSelfTestTrace("ui-perf-log-entry-limit");
        ok = false;
    }
    if (trimRebuildCount > 8) {
        writeSelfTestTrace("ui-perf-log-rebuild-thrash");
        ok = false;
    }

    char message[256] = {};
    std::snprintf(
        message,
        sizeof(message),
        "ui-perf %s tabs=%d tab-ms=%lld layout-pass=%llu apply=%llu revision=%llu log-lines=%d log-ms=%lld log-flush=%llu log-rebuild=%llu",
        ok ? "ok" : "failed",
        kIterations,
        static_cast<long long>(elapsedMs.count()),
        static_cast<unsigned long long>(window.layoutPassCount_),
        static_cast<unsigned long long>(window.workbenchTabState_.applyCount()),
        static_cast<unsigned long long>(window.workbenchTabState_.layoutRevision()),
        kLogIterations,
        static_cast<long long>(logElapsedMs.count()),
        static_cast<unsigned long long>(window.logFlushPassCount_),
        static_cast<unsigned long long>(trimRebuildCount));
    writeSelfTestTrace(message);

    DestroyWindow(window.window_);
    return ok;
}

LRESULT CALLBACK NativeMainWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeMainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<NativeMainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    if (self != nullptr) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

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

void NativeMainWindow::createMenus() {
    menu_ = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU serialMenu = CreatePopupMenu();
    HMENU toolsMenu = CreatePopupMenu();
    HMENU analysisMenu = CreatePopupMenu();
    HMENU viewMenu = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();

    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_SAVE_PROFILE, tx(T::FileSaveProfileMenu));
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, tx(T::FileExitMenu));

    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_REFRESH, tx(T::SerialRefreshMenu));
    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_CONNECT, tx(T::SerialConnectMenu));
    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_DISCONNECT, tx(T::SerialDisconnectMenu));
    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_AUTO_RECONNECT, tx(T::SerialAutoReconnectMenu));

    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_SEND, tx(T::ToolsSendMenu));
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_PAUSE_SCROLL, tx(T::ToolsPauseScrollMenu));
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_FOLLOW_LATEST_LOG, tx(T::ToolsFollowLatestLogMenu));
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_CLEAR_LOG, tx(T::ToolsClearLogMenu));
    AppendMenuW(toolsMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_FIND_LOG, tx(T::ToolsFindLogMenu));
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_COPY_LOG, tx(T::ToolsCopyLogMenu));
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_EXPORT_LOG, tx(T::ToolsExportLogMenu));

    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_MODBUS_SCAN, tx(T::AnalysisModbusScanMenu));
    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_WORKSPACE, tx(T::AnalysisWorkspaceMenu));
    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_RULE_VERIFY, tx(T::AnalysisRuleVerifyMenu));
    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_EXPORT_REPORT, tx(T::AnalysisExportReportMenu));

    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_THEME_DEFAULT, tx(T::ThemeDefaultMenu));
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_THEME_SOFT, tx(T::ThemeSoftMenu));
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_THEME_HIGH_CONTRAST, tx(T::ThemeHighContrastMenu));
    AppendMenuW(viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu, MF_STRING, IDM_VIEW_SHOW_TIMESTAMPS, tx(T::ShowLogTimestampsMenu));

    AppendMenuW(helpMenu, MF_STRING, IDM_HELP_ABOUT, tx(T::HelpAboutMenu));

    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), tx(T::FileMenu));
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(serialMenu), tx(T::SerialMenu));
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(toolsMenu), tx(T::ToolsMenu));
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(analysisMenu), tx(T::AnalysisMenu));
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu), tx(T::ViewMenu));
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), tx(T::HelpMenu));
    SetMenu(window_, menu_);
    updateLogTimestampMenu();
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
    fileProgress_ = createProgressControl(window_, instance_, IDC_FILE_PROGRESS);
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
    modbusProgress_ = createProgressControl(window_, instance_, IDC_MODBUS_PROGRESS);
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

void NativeMainWindow::updateModbusScanProgress(
    std::size_t completedBlocks,
    std::size_t totalBlocks,
    std::size_t successBlocks,
    std::size_t failedBlocks,
    std::size_t observations) {
    if (modbusProgress_ != nullptr) {
        SendMessageW(modbusProgress_, PBM_SETRANGE32, 0, 1000);
        const int position = totalBlocks == 0
            ? 0
            : static_cast<int>(std::min<std::size_t>(1000, (completedBlocks * 1000) / totalBlocks));
        SendMessageW(modbusProgress_, PBM_SETPOS, static_cast<WPARAM>(position), 0);
    }

    const std::wstring text = std::to_wstring(completedBlocks)
        + L"/"
        + std::to_wstring(totalBlocks)
        + L"  \u6210"
        + std::to_wstring(successBlocks)
        + L" \u5931"
        + std::to_wstring(failedBlocks)
        + L" \u89C2"
        + std::to_wstring(observations);
    if (modbusProgressText_ != nullptr) {
        SetWindowTextW(modbusProgressText_, text.c_str());
    }
}

void NativeMainWindow::pollSerial() {
    if (!serialPort_.isOpen()) {
        return;
    }
    if (!serialIoState_.allowsSerialPoll()) {
        return;
    }
    if (!serialPort_.waitForReadyRead(0)) {
        if (!serialPort_.lastErrorText().empty()) {
            handleSerialFailure(serialPort_.lastErrorText());
        }
        return;
    }

    std::vector<native_storage::RawIoEvent> events;
    std::vector<std::uint8_t> mergedPayload;
    const std::string endpoint = serialPort_.endpoint();
    for (int batch = 0; batch < 8; ++batch) {
        const std::vector<std::uint8_t> payload = serialPort_.readAvailable(4096);
        if (payload.empty()) {
            if (!serialPort_.lastErrorText().empty()) {
                handleSerialFailure(serialPort_.lastErrorText());
            }
            break;
        }
        native_storage::RawIoEvent event;
        event.sessionId = sessionId_;
        event.direction = "Rx";
        event.timestampUtc = nativeUtcTimestampText();
        event.endpoint = endpoint;
        event.payload = payload;
        events.push_back(std::move(event));
        mergedPayload.insert(mergedPayload.end(), payload.begin(), payload.end());
    }
    if (mergedPayload.empty()) {
        return;
    }
    saveRawEvents(std::move(events));
    appendPayloadLog(NativeLogKind::Rx, mergedPayload);
    statusCountersState_.addRxBytes(static_cast<std::uint64_t>(mergedPayload.size()));
    updateStatusSegments();
}

void NativeMainWindow::handleSerialFailure(const std::string& message) {
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::string endpoint = serialPort_.endpoint();
    stopFileSend({});
    serialPort_.close();
    updateConnectionButtonState();
    updateRtsControlState();
    appendLog(NativeLogKind::Error, uiString(T::SystemSerialFailedPrefix) + utf8ToWide(message));
    const bool autoReconnect = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (autoReconnect && reconnectState_.hasLastOpenOptions()) {
        reconnectState_.startWaiting(endpoint);
        SetTimer(window_, IDT_RECONNECT, 2000, nullptr);
        setStatus(uiString(T::ReconnectWaitingPrefix) + utf8ToWide(endpoint));
    } else {
        setStatus(utf8ToWide(message));
    }
}

void NativeMainWindow::tryAutoReconnect() {
    if (!reconnectState_.shouldTryReconnect(serialPort_.isOpen())) {
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    const auto ports = Win32SerialEnumerator::availablePorts();
    const bool present = std::any_of(ports.begin(), ports.end(), [this](const SerialPortDescriptor& port) {
        return normalizedComPortName(port.portName) == normalizedComPortName(reconnectState_.reconnectPortName());
    });
    if (!present) {
        setStatus(uiString(T::WaitingReconnectPrefix) + utf8ToWide(reconnectState_.reconnectPortName()) + uiString(T::PortNotReadySuffix));
        return;
    }

    const std::optional<SerialOpenOptions> options = reconnectState_.reconnectOptions();
    if (!options.has_value()) {
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }
    if (!serialPort_.open(*options)) {
        setStatus(uiString(T::AutoReconnectFailedPrefix) + utf8ToWide(serialPort_.lastErrorText()));
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    reconnectState_.markReconnectSucceeded();
    KillTimer(window_, IDT_RECONNECT);
    updateConnectionButtonState();
    updateRtsControlState();
    appendLog(uiString(T::SystemReconnectOkPrefix) + utf8ToWide(serialPort_.endpoint()));
    setStatus(tx(T::AutoReconnectOk));
}

void NativeMainWindow::setStatus(const std::wstring& text) {
    if (statusText_ != nullptr && cachedStatusText_ != text) {
        SetWindowTextW(statusText_, text.c_str());
        cachedStatusText_ = text;
    }
    updateStatusSegments();
}

void NativeMainWindow::updateStatusSegments() {
    if (txStatusText_ != nullptr) {
        const std::wstring txText = statusCountersState_.txStatusText();
        if (cachedTxStatusText_ != txText) {
            SetWindowTextW(txStatusText_, txText.c_str());
            cachedTxStatusText_ = txText;
        }
    }
    if (rxStatusText_ != nullptr) {
        const std::wstring rxText = statusCountersState_.rxStatusText();
        if (cachedRxStatusText_ != rxText) {
            SetWindowTextW(rxStatusText_, rxText.c_str());
            cachedRxStatusText_ = rxText;
        }
    }
    if (clockStatusText_ != nullptr) {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        wchar_t buffer[16] = {};
        swprintf_s(
            buffer,
            L"%02u:%02u:%02u",
            static_cast<unsigned int>(now.wHour),
            static_cast<unsigned int>(now.wMinute),
            static_cast<unsigned int>(now.wSecond));
        const std::wstring clockText(buffer);
        if (cachedClockStatusText_ != clockText) {
            SetWindowTextW(clockStatusText_, clockText.c_str());
            cachedClockStatusText_ = clockText;
        }
    }
}

void NativeMainWindow::setSendModeStatus() {
    const int mode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    switch (mode) {
    case 1:
        setStatus(tx(T::SendModeHexStatus));
        break;
    case 2:
        setStatus(tx(T::SendModeDecimalStatus));
        break;
    case 3:
        setStatus(tx(T::SendModeBinaryStatus));
        break;
    default:
        setStatus(tx(T::SendModeTextStatus));
        break;
    }
}

std::wstring NativeMainWindow::controlText(HWND control) const {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::wstring NativeMainWindow::analysisInputText(HWND control) const {
    return controlText(control);
}

void NativeMainWindow::setControlText(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

std::vector<std::uint8_t> NativeMainWindow::payloadFromInput(std::wstring* errorText) const {
    return payloadFromText(controlText(sendEdit_), errorText);
}

std::vector<std::uint8_t> NativeMainWindow::payloadFromText(const std::wstring& text, std::wstring* errorText) const {
    if (errorText != nullptr) {
        errorText->clear();
    }
    NativeSendCodecErrors errors;
    errors.hexInvalidChar = tx(T::HexInvalidChar);
    errors.hexOddNibble = tx(T::HexOddNibble);
    errors.invalidDecimal = tx(T::SendModeInvalidDecimal);
    errors.invalidBinary = tx(T::SendModeInvalidBinary);
    errors.textEncodingFailed = tx(T::TextEncodingFailed);

    NativeSendPayloadOptions options;
    options.mode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    options.textCodePage = selectedTextCodePage();
    options.lineEnding = static_cast<int>(selectedComboData(lineEndingCombo_, 0));

    const NativeSendPayloadResult result = nativeBuildSendPayload(text, options, errors);
    if (errorText != nullptr) {
        *errorText = result.errorText;
    }
    return result.payload;
}

std::wstring NativeMainWindow::formatPayloadForLog(const std::vector<std::uint8_t>& payload) const {
    const int mode = static_cast<int>(selectedComboData(logFormatCombo_, 0));
    switch (mode) {
    case 1:
        return nativeBytesToDecimal(payload);
    case 2:
        return nativeBytesToBinary(payload);
    case 3:
        return nativeDecodeBytesToText(payload, selectedLogCodePage());
    case 4:
        return nativeBytesToHex(payload) + L" | " + nativeDecodeBytesToText(payload, selectedLogCodePage());
    default:
        return nativeBytesToHex(payload);
    }
}

unsigned int NativeMainWindow::selectedTextCodePage() const {
    return static_cast<unsigned int>(selectedComboData(textEncodingCombo_, CP_UTF8));
}

unsigned int NativeMainWindow::selectedLogCodePage() const {
    return static_cast<unsigned int>(selectedComboData(logEncodingCombo_, CP_UTF8));
}

void NativeMainWindow::runModbusScan() {
    if (serialIoState_.isOwnedBy(NativeSerialIoOwner::ModbusScan)) {
        requestCancelModbusScan();
        return;
    }
    if (!serialPort_.isOpen()) {
        setStatus(tx(T::ConnectBeforeModbus));
        return;
    }
    if (!store_.isOpen()) {
        setStatus(tx(T::StorageModbusClosed));
        return;
    }
    if (!serialIoState_.allowsModbusScan()) {
        setStatus(serialIoBusyStatus());
        return;
    }

    NativeModbusScanRequestInput requestInput;
    requestInput.slaveId = textToInt(scanSlaveEdit_, 1);
    requestInput.functionCode = static_cast<::svm::core::Byte>(selectedComboData(scanFunctionCombo_, 3));
    requestInput.startAddress = textToInt(scanStartEdit_, 0);
    requestInput.endAddress = textToInt(scanEndEdit_, 15);
    requestInput.scanSessionId = "scan-" + nativeTimestampIdText();
    requestInput.startedAtUtc = nativeUtcTimestampText();

    auto requestResult = nativeBuildModbusScanRequest(requestInput);
    if (!requestResult.ok) {
        setStatus(utf8ToWide(requestResult.errorMessage));
        MessageBoxW(window_, utf8ToWide(requestResult.errorMessage).c_str(), tx(T::ModbusInvalidTitle), MB_ICONWARNING | MB_OK);
        return;
    }

    appendLog(uiString(T::SystemModbusStartPrefix) + utf8ToWide(requestInput.scanSessionId));
    closeModbusScanThread();
    modbusScanCancelRequested_ = false;
    disconnectAfterModbusScan_ = false;
    if (!serialIoState_.tryAcquire(NativeSerialIoOwner::ModbusScan)) {
        setStatus(serialIoBusyStatus());
        return;
    }
    setModbusScanRunningUi(true);
    updateModbusScanProgress(0, requestResult.request.plan.blocks.size(), 0, 0, 0);
    setStatus(tx(T::ModbusRunning));

    auto context = std::make_unique<NativeModbusScanContext>();
    context->notifyWindow = window_;
    context->serialPort = &serialPort_;
    context->cancelRequested = &modbusScanCancelRequested_;
    context->plan = std::move(requestResult.request.plan);
    context->execution = std::move(requestResult.request.execution);
    context->scanSessionId = requestInput.scanSessionId;
    context->timeoutErrorMessage = wideToUtf8(tx(T::ModbusTimeout));
    context->threadExceptionMessage = wideToUtf8(L"Modbus \u626B\u63CF\u7EBF\u7A0B\u5F02\u5E38\u7EC8\u6B62\u3002");
    modbusScanThread_ = CreateThread(nullptr, 0, &nativeModbusScanThreadProc, context.get(), 0, nullptr);
    if (modbusScanThread_ == nullptr) {
        setModbusScanRunningUi(false);
        setStatus(tx(T::ModbusThreadCreateFailed));
        return;
    }
    context.release();
}

void NativeMainWindow::requestCancelModbusScan() {
    if (!modbusScanRunning_) {
        return;
    }
    modbusScanCancelRequested_ = true;
    setStatus(tx(T::ModbusCancelRequestedStatus));
}

void NativeMainWindow::handleModbusScanProgress(NativeModbusScanProgress* progressPointer) {
    std::unique_ptr<NativeModbusScanProgress> progress(progressPointer);
    if (!progress) {
        return;
    }

    updateModbusScanProgress(
        progress->completedBlocks,
        progress->totalBlocks,
        progress->successBlocks,
        progress->failedBlocks,
        progress->observations);
    if (modbusScanRunning_) {
        setStatus(uiString(T::ModbusProgressPrefix)
            + std::to_wstring(progress->completedBlocks)
            + L"/"
            + std::to_wstring(progress->totalBlocks)
            + tx(T::ModbusProgressBlocks)
            + tx(T::ModbusProgressSuccessBlocks)
            + std::to_wstring(progress->successBlocks)
            + tx(T::ModbusProgressFailedBlocks)
            + std::to_wstring(progress->failedBlocks)
            + tx(T::ModbusProgressObservationsShort)
            + std::to_wstring(progress->observations)
            + tx(T::ChinesePeriod));
    }
}

void NativeMainWindow::handleModbusScanDataBatch(NativeModbusScanDataBatch* batchPointer) {
    std::unique_ptr<NativeModbusScanDataBatch> batch(batchPointer);
    if (!batch) {
        return;
    }

    for (const native_storage::RawIoEvent& event : batch->rawEvents) {
        if (event.direction == "Tx") {
            statusCountersState_.addTxBytes(static_cast<std::uint64_t>(event.payload.size()));
        } else if (event.direction == "Rx") {
            statusCountersState_.addRxBytes(static_cast<std::uint64_t>(event.payload.size()));
        }
    }
    if (!batch->rawEvents.empty()) {
        updateStatusSegments();
        saveRawEvents(std::move(batch->rawEvents));
    }
    for (NativeLogEntry& entry : batch->logEntries) {
        addLogEntry(std::move(entry));
    }
}

void NativeMainWindow::handleModbusScanDone(NativeModbusScanResult* resultPointer) {
    std::unique_ptr<NativeModbusScanResult> result(resultPointer);
    const bool shouldDisconnectAfterScan = disconnectAfterModbusScan_;
    disconnectAfterModbusScan_ = false;
    closeModbusScanThread();
    if (!result) {
        setModbusScanRunningUi(false);
        return;
    }

    updateCompletedModbusScanProgress(*result);
    const std::wstring summary = persistCompletedModbusScan(*result);
    if (!summary.empty()) {
        appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    }
    setModbusScanRunningUi(false);
    if (handleCompletedModbusScanDisconnect(*result, shouldDisconnectAfterScan)) {
        return;
    }
    if (result->serialFailed && !result->errorMessage.empty()) {
        handleSerialFailure(result->errorMessage);
        return;
    }
    setStatus(summary);
}

void NativeMainWindow::closeModbusScanThread() {
    if (modbusScanThread_ == nullptr) {
        return;
    }
    WaitForSingleObject(modbusScanThread_, INFINITE);
    CloseHandle(modbusScanThread_);
    modbusScanThread_ = nullptr;
}

void NativeMainWindow::updateCompletedModbusScanProgress(const NativeModbusScanResult& result) {
    updateModbusScanProgress(
        result.execution.attempts.size(),
        static_cast<std::size_t>(std::max(0, result.execution.session.requestCount)),
        static_cast<std::size_t>(std::max(0, result.execution.session.successBlockCount)),
        static_cast<std::size_t>(std::max(0, result.execution.session.failedBlockCount)),
        result.execution.observations.size());
}

std::wstring NativeMainWindow::persistCompletedModbusScan(const NativeModbusScanResult& result) {
    if (!store_.saveScanExecution(result.execution)) {
        return utf8ToWide(store_.lastErrorText());
    }
    if (result.cancelled) {
        return tx(T::ModbusCancelledStatus);
    }
    return uiString(T::ModbusSummaryPrefix)
        + std::to_wstring(result.execution.session.successBlockCount)
        + uiString(T::ModbusFailedBlocks)
        + std::to_wstring(result.execution.session.failedBlockCount)
        + uiString(T::ModbusObservations)
        + std::to_wstring(result.execution.observations.size())
        + uiString(T::ChinesePeriod);
}

bool NativeMainWindow::handleCompletedModbusScanDisconnect(const NativeModbusScanResult& result, bool shouldDisconnectAfterScan) {
    if (!shouldDisconnectAfterScan) {
        return false;
    }
    if (result.serialFailed && !result.errorMessage.empty()) {
        appendLog(NativeLogKind::Error, uiString(T::SystemSerialFailedPrefix) + utf8ToWide(result.errorMessage));
    }
    closeSerialPort(tx(T::ModbusDisconnectedAfterCancelStatus));
    return true;
}

void NativeMainWindow::setModbusScanRunningUi(bool running) {
    modbusScanRunning_ = running;
    if (running && !serialIoState_.isOwnedBy(NativeSerialIoOwner::ModbusScan)) {
        serialIoState_.tryAcquire(NativeSerialIoOwner::ModbusScan);
    } else if (!running) {
        serialIoState_.release(NativeSerialIoOwner::ModbusScan);
    }
    if (running) {
        KillTimer(window_, IDT_SERIAL_POLL);
        KillTimer(window_, IDT_TIMED_SEND);
    } else if (serialPort_.isOpen()) {
        SetTimer(window_, IDT_SERIAL_POLL, 50, nullptr);
        updateTimedSendTimer();
    }
    const NativeModbusScanUiSnapshot ui = modbusScanUiState_.snapshot(running);
    SetWindowTextW(modbusButton_, ui.buttonMode == NativeModbusScanButtonMode::Stop ? tx(T::ModbusStopButton) : tx(T::ModbusScanButton));

    for (HWND control : {
             portCombo_,
             refreshButton_,
             saveProfileButton_,
             baudCombo_,
             dataBitsCombo_,
             parityCombo_,
             stopBitsCombo_,
             flowControlCombo_,
             dtrCheck_,
             rtsCheck_,
             autoReconnectCheck_,
             scanSlaveEdit_,
             scanFunctionCombo_,
             scanStartEdit_,
             scanEndEdit_,
             analysisButton_,
             ruleVerifyButton_,
             exportReportButton_,
             sendEdit_,
             sendButton_,
             timedSendCheck_,
             timedPeriodEdit_,
             fileSendButton_,
             fileBrowseButton_,
             filePathEdit_,
         }) {
        enableControl(control, ui.exclusiveControlsEnabled);
    }
    for (HWND button : quickSendButtons_) {
        enableControl(button, ui.exclusiveControlsEnabled);
    }
    updateRtsControlState();
}

void NativeMainWindow::showAnalysisWorkspace() {
    if (!store_.isOpen()) {
        setStatus(tx(T::AnalysisNoStore));
        return;
    }
    const auto session = store_.latestScanSession();
    if (!session.has_value()) {
        setStatus(tx(T::AnalysisNoScan));
        return;
    }

    const auto observations = store_.scanObservations(session->sessionId);
    if (observations.empty()) {
        setStatus(tx(T::AnalysisNoObservations));
        return;
    }

    bool targetOk = false;
    const double targetValue = textToDoubleText(analysisInputText(targetValueEdit_), 0.0, &targetOk);
    if (!targetOk) {
        setStatus(tx(T::AnalysisInvalidTarget));
        return;
    }
    bool toleranceOk = false;
    const double tolerance = std::max(0.0, textToDouble(toleranceEdit_, 0.0, &toleranceOk));

    const std::string runId = "match-" + nativeTimestampIdText();
    const NativeCandidateAnalysisBuildResult analysis = nativeBuildCandidateAnalysisRun(
        *session,
        observations,
        wideToUtf8(analysisInputText(targetLabelEdit_)),
        targetValue,
        wideToUtf8(analysisInputText(targetUnitEdit_)),
        toleranceOk ? tolerance : 0.0,
        runId,
        nativeUtcTimestampText(),
        nativeUtcTimestampText(),
        nativeUtcTimestampText());
    if (!analysis.success) {
        setStatus(utf8ToWide(analysis.errorMessage));
        return;
    }
    if (analysis.candidates.empty()) {
        setStatus(tx(T::AnalysisNoCandidates));
        return;
    }

    if (!store_.saveMatchRun(analysis.run, analysis.candidates)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return;
    }
    candidateCacheState_.setLatestMatchRunId(runId);
    refreshCandidateCombo(runId);

    const std::wstring summary = uiString(T::AnalysisSavedPrefix)
        + utf8ToWide(session->sessionId)
        + uiString(T::AnalysisSavedMid)
        + std::to_wstring(analysis.candidates.size())
        + uiString(T::AnalysisSavedRun)
        + utf8ToWide(runId)
        + uiString(T::ChinesePeriod);
    appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    setStatus(summary);
}

void NativeMainWindow::showRuleVerification() {
    if (!store_.isOpen()) {
        setStatus(tx(T::RuleNoStore));
        return;
    }

    if (const auto candidate = selectedCandidate(); candidate.has_value()) {
        if (!saveRuleFromCandidate(*candidate)) {
            return;
        }
    } else if (store_.recentProtocolFieldRules(1).empty()) {
        setStatus(tx(T::RuleNoCandidates));
        return;
    }

    const auto session = store_.latestScanSession();
    if (!session.has_value()) {
        setStatus(tx(T::RuleVerifyNoScan));
        return;
    }
    runRuleVerification(*session);
}

void NativeMainWindow::exportReport() {
    if (!store_.isOpen()) {
        setStatus(tx(T::RuleNoStore));
        return;
    }

    std::optional<native_storage::RuleVerificationRunRecord> run;
    if (candidateCacheState_.hasLatestVerificationRunId()) {
        run = store_.ruleVerificationRun(candidateCacheState_.latestVerificationRunId());
    }
    if (!run.has_value()) {
        run = store_.latestRuleVerificationRun();
    }
    if (!run.has_value()) {
        setStatus(tx(T::ExportNoRun));
        return;
    }

    const auto resultRecords = store_.ruleVerificationResults(run->verificationRunId);

    wchar_t fileName[MAX_PATH] = {};
    const std::wstring defaultName = uiString(T::ExportDefaultPrefix)
        + utf8ToWide(run->verificationRunId)
        + L".md";
    wcsncpy_s(fileName, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = tx(T::ExportFilter);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"md";
    dialog.lpstrTitle = tx(T::ExportDialogTitle);
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }

    const std::string markdown = nativeRenderRuleVerificationMarkdownReport(*run, resultRecords);
    std::ofstream output(std::filesystem::path(fileName), std::ios::binary | std::ios::trunc);
    if (!output) {
        setStatus(uiString(T::ExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    output.write(markdown.data(), static_cast<std::streamsize>(markdown.size()));
    if (!output) {
        setStatus(uiString(T::ExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    setStatus(uiString(T::ExportOkPrefix) + std::wstring(fileName) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::showAbout() {
    MessageBoxW(
        window_,
        tx(T::AboutText),
        tx(T::AboutTitle),
        MB_ICONINFORMATION | MB_OK);
}

void NativeMainWindow::showDeferredFeature(const std::wstring& title, const std::wstring& message) {
    MessageBoxW(window_, message.c_str(), title.c_str(), MB_ICONINFORMATION | MB_OK);
    setStatus(title + L"\uFF1A" + message);
}

void NativeMainWindow::refreshCandidateCombo(const std::string& runId) {
    SendMessageW(candidateCombo_, CB_RESETCONTENT, 0, 0);
    addComboItem(candidateCombo_, tx(T::CandidatePlaceholder), 0);
    if (!loadCandidateCache(runId)) {
        SendMessageW(candidateCombo_, CB_SETCURSEL, 0, 0);
        return;
    }

    for (const native_storage::MatchCandidateRecord& candidate : candidateCacheState_.candidates()) {
        const std::wstring display = nativeCandidateDisplayText(candidate);
        const LRESULT index = SendMessageW(candidateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        if (index >= 0) {
            SendMessageW(candidateCombo_, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(candidate.id));
        }
    }
    SendMessageW(candidateCombo_, CB_SETCURSEL, candidateCacheState_.candidatesEmpty() ? 0 : 1, 0);
}

bool NativeMainWindow::loadCandidateCache(const std::string& runId) {
    if (!store_.isOpen() || runId.empty()) {
        candidateCacheState_.clearCandidateCache();
        return false;
    }
    if (candidateCacheState_.isCandidateCacheFor(runId)) {
        return true;
    }
    candidateCacheState_.setCandidateCache(runId, store_.matchCandidates(runId));
    return true;
}

std::optional<native_storage::MatchCandidateRecord> NativeMainWindow::selectedCandidate() {
    if (!store_.isOpen()) {
        return std::nullopt;
    }
    std::string runId = candidateCacheState_.latestMatchRunId();
    if (runId.empty()) {
        const auto run = store_.latestMatchRun();
        if (!run.has_value()) {
            return std::nullopt;
        }
        runId = run->runId;
        candidateCacheState_.setLatestMatchRunId(runId);
    }

    if (!loadCandidateCache(runId) || candidateCacheState_.candidatesEmpty()) {
        return std::nullopt;
    }
    const LRESULT index = SendMessageW(candidateCombo_, CB_GETCURSEL, 0, 0);
    const LRESULT itemData = index >= 0 ? SendMessageW(candidateCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) : 0;
    const std::int64_t selectedId = itemData == CB_ERR ? 0 : static_cast<std::int64_t>(itemData);
    if (const auto candidate = candidateCacheState_.candidateById(selectedId); candidate.has_value()) {
        return candidate;
    }
    return candidateCacheState_.defaultCandidate();
}

bool NativeMainWindow::saveRuleFromCandidate(const native_storage::MatchCandidateRecord& candidate) {
    if (!store_.isOpen()) {
        setStatus(tx(T::RuleNoStore));
        return false;
    }
    if (candidate.id <= 0) {
        setStatus(tx(T::RuleNoCandidates));
        return false;
    }

    native_storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = "rule-" + std::to_string(candidate.id);
    rule.fieldName = wideToUtf8(nativeRuleDisplayName(candidate, analysisInputText(targetLabelEdit_)));
    rule.sourceStabilityRunId = candidate.runId;
    rule.sourceStableCandidateId = candidate.id;
    rule.candidateType = candidate.candidateType;
    rule.wordOrder = candidate.wordOrder;
    rule.byteOrder = candidate.byteOrder;
    rule.slaveId = candidate.slaveId;
    rule.functionCode = candidate.functionCode;
    rule.startAddress = candidate.startAddress;
    rule.registerCount = candidate.registerCount;
    rule.scaleMultiplier = candidate.scaleMultiplier;
    rule.scaleOffset = candidate.scaleOffset;
    rule.unit = wideToUtf8(analysisInputText(targetUnitEdit_));
    rule.confidenceLevel = wideToUtf8(candidate.score >= 85.0 ? L"\u9AD8" : (candidate.score >= 65.0 ? L"\u4E2D" : L"\u4F4E"));
    rule.stabilityScore = candidate.score;
    rule.evidenceSummary = candidate.evidenceText;
    rule.createdAtUtc = nativeUtcTimestampText();
    if (!store_.saveProtocolFieldRule(rule)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return false;
    }

    setStatus(uiString(T::RuleSavedPrefix) + utf8ToWide(rule.fieldName) + L" (" + utf8ToWide(rule.ruleId) + L")" + uiString(T::ChinesePeriod));
    return true;
}

bool NativeMainWindow::runRuleVerification(const native_storage::ScanSessionRecord& session) {
    if (!store_.isOpen()) {
        setStatus(tx(T::RuleNoStore));
        return false;
    }

    const auto rules = store_.recentProtocolFieldRules(200);
    if (rules.empty()) {
        setStatus(tx(T::RuleVerifyNoRules));
        return false;
    }
    const auto observations = store_.scanObservations(session.sessionId);
    if (observations.empty()) {
        setStatus(tx(T::RuleVerifyNoObservations));
        return false;
    }

    const NativeRuleVerificationBuildResult verification = nativeBuildRuleVerificationResult(
        session,
        rules,
        observations,
        "verify-" + nativeTimestampIdText(),
        nativeUtcTimestampText());

    if (!store_.saveRuleVerificationRun(verification.run, verification.results)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return false;
    }
    candidateCacheState_.setLatestVerificationRunId(verification.run.verificationRunId);

    const std::wstring summary = uiString(T::RuleVerifySavedPrefix)
        + utf8ToWide(verification.run.verificationRunId)
        + uiString(T::RulesTotalPrefix)
        + std::to_wstring(verification.run.ruleCount)
        + uiString(T::RulesVerifiedPrefix)
        + std::to_wstring(verification.run.verifiedCount)
        + uiString(T::RulesMissingPrefix)
        + std::to_wstring(verification.run.missingCount)
        + uiString(T::RulesUnsupportedPrefix)
        + std::to_wstring(verification.run.unsupportedCount)
        + uiString(T::ChinesePeriod);
    appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    setStatus(summary);
    return true;
}

std::filesystem::path NativeMainWindow::defaultStoreDirectory() const {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(localAppData) / L"SerialValueMatcherNative" / L"native-store";
    }
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    return std::filesystem::path(tempPath) / L"SerialValueMatcherNative" / L"native-store";
}

void NativeMainWindow::saveRawEvent(std::string direction, const std::vector<std::uint8_t>& payload) {
    if (!store_.isOpen()) {
        return;
    }
    native_storage::RawIoEvent event;
    event.sessionId = sessionId_;
    event.direction = std::move(direction);
    event.timestampUtc = nativeUtcTimestampText();
    event.endpoint = serialPort_.endpoint();
    event.payload = payload;
    saveRawEvents({std::move(event)});
}

bool NativeMainWindow::saveRawEvents(std::vector<native_storage::RawIoEvent> events) {
    if (!store_.isOpen() || events.empty()) {
        return false;
    }
    return store_.appendRawEvents(events);
}

} // namespace svm::win32

#endif
