#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/resource.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_enumerator.h"
#include "win32/win32_serial_types.h"

#include "core/modbus_core.h"

#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <sstream>
#include <utility>

namespace svm::win32 {
namespace {

constexpr wchar_t kWindowClassName[] = L"SvmNativeMainWindow";
constexpr std::size_t kMaxLogChars = 200000;

std::wstring bytesToHex(const std::vector<std::uint8_t>& bytes) {
    std::wostringstream output;
    output.setf(std::ios::uppercase);
    output << std::hex;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << L' ';
        }
        output.width(2);
        output.fill(L'0');
        output << static_cast<int>(bytes[index]);
    }
    return output.str();
}

int hexValue(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f') {
        return 10 + ch - L'a';
    }
    if (ch >= L'A' && ch <= L'F') {
        return 10 + ch - L'A';
    }
    return -1;
}

std::vector<std::uint8_t> parseHexPayload(std::wstring_view text, std::wstring* errorText) {
    std::vector<int> nibbles;
    for (wchar_t ch : text) {
        if (std::iswspace(ch) || ch == L',' || ch == L'-') {
            continue;
        }
        const int value = hexValue(ch);
        if (value < 0) {
            if (errorText != nullptr) {
                *errorText = L"HEX 输入包含非十六进制字符。";
            }
            return {};
        }
        nibbles.push_back(value);
    }

    if (nibbles.empty()) {
        return {};
    }
    if ((nibbles.size() % 2) != 0) {
        if (errorText != nullptr) {
            *errorText = L"HEX 输入的半字节数量为奇数，请补齐。";
        }
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(nibbles.size() / 2);
    for (std::size_t index = 0; index < nibbles.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>((nibbles[index] << 4) | nibbles[index + 1]));
    }
    return bytes;
}

std::string timestampText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
    gmtime_s(&utc, &time);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

void addControlFont(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void addComboItem(HWND combo, const wchar_t* text, LPARAM data) {
    const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    if (index >= 0) {
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), data);
    }
}

LPARAM selectedComboData(HWND combo, LPARAM fallback = 0) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0) {
        return fallback;
    }
    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    return data == CB_ERR ? fallback : data;
}

void selectComboData(HWND combo, LPARAM data) {
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        if (SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) == data) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
            return;
        }
    }
    if (count > 0) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

std::string parityKey(SerialParity parity) {
    switch (parity) {
    case SerialParity::None:
        return "None";
    case SerialParity::Odd:
        return "Odd";
    case SerialParity::Even:
        return "Even";
    case SerialParity::Mark:
        return "Mark";
    case SerialParity::Space:
        return "Space";
    }
    return "None";
}

SerialParity parityFromKey(std::string_view key) {
    if (key == "Odd") {
        return SerialParity::Odd;
    }
    if (key == "Even") {
        return SerialParity::Even;
    }
    if (key == "Mark") {
        return SerialParity::Mark;
    }
    if (key == "Space") {
        return SerialParity::Space;
    }
    return SerialParity::None;
}

std::string stopBitsKey(SerialStopBits stopBits) {
    switch (stopBits) {
    case SerialStopBits::One:
        return "One";
    case SerialStopBits::OnePointFive:
        return "OnePointFive";
    case SerialStopBits::Two:
        return "Two";
    }
    return "One";
}

SerialStopBits stopBitsFromKey(std::string_view key) {
    if (key == "OnePointFive") {
        return SerialStopBits::OnePointFive;
    }
    if (key == "Two") {
        return SerialStopBits::Two;
    }
    return SerialStopBits::One;
}

std::string flowControlKey(SerialFlowControl flowControl) {
    switch (flowControl) {
    case SerialFlowControl::None:
        return "None";
    case SerialFlowControl::HardwareRtsCts:
        return "Hardware";
    case SerialFlowControl::SoftwareXonXoff:
        return "Software";
    }
    return "None";
}

SerialFlowControl flowControlFromKey(std::string_view key) {
    if (key == "Hardware") {
        return SerialFlowControl::HardwareRtsCts;
    }
    if (key == "Software") {
        return SerialFlowControl::SoftwareXonXoff;
    }
    return SerialFlowControl::None;
}

void appendLineEnding(std::vector<std::uint8_t>& payload, int lineEnding) {
    switch (lineEnding) {
    case 1:
        payload.push_back('\r');
        break;
    case 2:
        payload.push_back('\n');
        break;
    case 3:
        payload.push_back('\r');
        payload.push_back('\n');
        break;
    default:
        break;
    }
}

int textToInt(HWND control, int fallback) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return fallback;
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    return end != text.c_str() ? static_cast<int>(value) : fallback;
}

} // namespace

bool NativeMainWindow::create(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = NativeMainWindow::windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass)) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        L"串口值匹配器 Win32 Native",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1180,
        760,
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
    const std::wstring chineseText = L"串口值匹配器";
    if (utf8ToWide("串口值匹配器") != chineseText || wideToUtf8(chineseText) != "串口值匹配器") {
        return false;
    }

    SerialOpenOptions options;
    options.portName = "COM1";
    if (!validateSerialOpenOptions(options).ok) {
        return false;
    }
    if (makeWin32DevicePath("COM10") != R"(\\.\COM10)") {
        return false;
    }

    wchar_t tempPathBuffer[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPathBuffer) == 0) {
        return false;
    }

    const auto storePath = std::filesystem::path(tempPathBuffer) / L"svm-native-win32-self-test";
    std::filesystem::remove_all(storePath);
    native_storage::NativeSessionStore store;
    if (!store.open(storePath)) {
        return false;
    }

    native_storage::RawIoEvent event;
    event.sessionId = "self-test";
    event.direction = "Tx";
    event.timestampUtc = timestampText();
    event.endpoint = "COM1";
    event.payload = {0x01, 0x03, 0x00, 0x00};

    native_storage::SerialProfile profile;
    profile.portName = "COM1";
    profile.baudRate = 115200;
    profile.dataBits = 8;
    profile.parity = "None";
    profile.stopBits = "One";
    profile.flowControl = "None";
    profile.updatedAtUtc = timestampText();

    native_storage::SendHistoryEntry history;
    history.content = "AT+测试";
    history.payloadMode = 0;
    history.lineEnding = 0;
    history.sentAtUtc = timestampText();

    const bool ok = store.appendRawEvent(event)
        && store.rawEventCount() == 1
        && store.saveSerialProfile(profile)
        && store.latestSerialProfile().has_value()
        && store.saveSendHistory(history)
        && !store.recentSendHistory(1).empty();
    std::filesystem::remove_all(storePath);
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
    case WM_CREATE:
        createMenus();
        createControls();
        if (!store_.open(defaultStoreDirectory())) {
            setStatus(utf8ToWide(store_.lastErrorText()));
        }
        refreshPorts();
        applyLatestSerialProfile();
        refreshSendHistory();
        SetTimer(window_, IDT_SERIAL_POLL, 50, nullptr);
        return 0;
    case WM_SIZE:
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
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
            SetWindowTextW(receiveLog_, L"");
            hiddenLogLineCount_ = 0;
            setStatus(L"接收区已清空，native 存储记录不会删除。");
            return 0;
        case IDC_SAVE_PROFILE_BUTTON:
            saveCurrentSerialProfile();
            return 0;
        case IDC_PAUSE_SCROLL_BUTTON:
            scrollPaused_ = !scrollPaused_;
            SetWindowTextW(pauseScrollButton_, scrollPaused_ ? L"恢复滚动" : L"暂停滚动");
            setStatus(scrollPaused_ ? L"已暂停滚动：仍继续接收和写入 native 存储。" : L"已恢复滚动。");
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
            setStatus(SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"已开启自动重连。" : L"已关闭自动重连。");
            return 0;
        case IDM_TOOLS_SEND:
            sendPayload();
            return 0;
        case IDM_TOOLS_PAUSE_SCROLL:
            scrollPaused_ = !scrollPaused_;
            SetWindowTextW(pauseScrollButton_, scrollPaused_ ? L"恢复滚动" : L"暂停滚动");
            setStatus(scrollPaused_ ? L"已暂停滚动：仍继续接收和写入 native 存储。" : L"已恢复滚动。");
            return 0;
        case IDM_TOOLS_CLEAR_LOG:
            SetWindowTextW(receiveLog_, L"");
            hiddenLogLineCount_ = 0;
            setStatus(L"接收区已清空，native 存储记录不会删除。");
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
        case IDM_HELP_ABOUT:
            showAbout();
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (wParam == IDT_SERIAL_POLL) {
            pollSerial();
            return 0;
        }
        if (wParam == IDT_RECONNECT) {
            tryAutoReconnect();
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        KillTimer(window_, IDT_SERIAL_POLL);
        KillTimer(window_, IDT_RECONNECT);
        disconnectSerial();
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
    HMENU helpMenu = CreatePopupMenu();

    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_SAVE_PROFILE, L"保存串口配置(&S)");
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, IDM_FILE_EXIT, L"退出(&X)");

    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_REFRESH, L"刷新端口(&R)");
    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_CONNECT, L"连接(&C)");
    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_DISCONNECT, L"断开(&D)");
    AppendMenuW(serialMenu, MF_STRING, IDM_SERIAL_AUTO_RECONNECT, L"切换自动重连(&A)");

    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_SEND, L"发送(&E)");
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_PAUSE_SCROLL, L"暂停/恢复滚动(&P)");
    AppendMenuW(toolsMenu, MF_STRING, IDM_TOOLS_CLEAR_LOG, L"清空接收区(&L)");

    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_MODBUS_SCAN, L"Modbus 扫描(&M)");
    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_WORKSPACE, L"分析工作区(&W)");
    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_RULE_VERIFY, L"规则验证(&V)");
    AppendMenuW(analysisMenu, MF_STRING, IDM_ANALYSIS_EXPORT_REPORT, L"导出验证报告(&O)");

    AppendMenuW(helpMenu, MF_STRING, IDM_HELP_ABOUT, L"关于(&A)");

    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu), L"文件(&F)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(serialMenu), L"串口(&S)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(toolsMenu), L"工具(&T)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(analysisMenu), L"分析(&A)");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu), L"帮助(&H)");
    SetMenu(window_, menu_);
}

void NativeMainWindow::createControls() {
    uiFont_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    portLabel_ = CreateWindowExW(0, L"STATIC", L"串口", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    portCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PORT_COMBO), instance_, nullptr);
    refreshButton_ = CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_REFRESH_BUTTON), instance_, nullptr);
    saveProfileButton_ = CreateWindowExW(0, L"BUTTON", L"保存配置", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SAVE_PROFILE_BUTTON), instance_, nullptr);
    baudLabel_ = CreateWindowExW(0, L"STATIC", L"波特率", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    baudCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_BAUD_EDIT), instance_, nullptr);
    dataBitsLabel_ = CreateWindowExW(0, L"STATIC", L"数据位", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    dataBitsCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DATA_BITS_COMBO), instance_, nullptr);
    parityLabel_ = CreateWindowExW(0, L"STATIC", L"校验", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    parityCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PARITY_COMBO), instance_, nullptr);
    stopBitsLabel_ = CreateWindowExW(0, L"STATIC", L"停止位", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    stopBitsCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_STOP_BITS_COMBO), instance_, nullptr);
    flowControlLabel_ = CreateWindowExW(0, L"STATIC", L"流控", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    flowControlCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_FLOW_CONTROL_COMBO), instance_, nullptr);
    dtrCheck_ = CreateWindowExW(0, L"BUTTON", L"DTR", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DTR_CHECK), instance_, nullptr);
    rtsCheck_ = CreateWindowExW(0, L"BUTTON", L"RTS", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RTS_CHECK), instance_, nullptr);
    autoReconnectCheck_ = CreateWindowExW(0, L"BUTTON", L"自动重连", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_AUTO_RECONNECT_CHECK), instance_, nullptr);
    connectButton_ = CreateWindowExW(0, L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CONNECT_BUTTON), instance_, nullptr);
    disconnectButton_ = CreateWindowExW(0, L"BUTTON", L"断开", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DISCONNECT_BUTTON), instance_, nullptr);
    sendModeCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_MODE_COMBO), instance_, nullptr);
    lineEndingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LINE_ENDING_COMBO), instance_, nullptr);
    historyCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_HISTORY_COMBO), instance_, nullptr);
    sendEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_EDIT), instance_, nullptr);
    sendButton_ = CreateWindowExW(0, L"BUTTON", L"发送", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_BUTTON), instance_, nullptr);
    pauseScrollButton_ = CreateWindowExW(0, L"BUTTON", L"暂停滚动", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PAUSE_SCROLL_BUTTON), instance_, nullptr);
    clearButton_ = CreateWindowExW(0, L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CLEAR_BUTTON), instance_, nullptr);
    modbusButton_ = CreateWindowExW(0, L"BUTTON", L"Modbus 扫描", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_MODBUS_BUTTON), instance_, nullptr);
    analysisButton_ = CreateWindowExW(0, L"BUTTON", L"分析工作区", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_ANALYSIS_BUTTON), instance_, nullptr);
    ruleVerifyButton_ = CreateWindowExW(0, L"BUTTON", L"规则验证", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RULE_VERIFY_BUTTON), instance_, nullptr);
    exportReportButton_ = CreateWindowExW(0, L"BUTTON", L"导出报告", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_EXPORT_REPORT_BUTTON), instance_, nullptr);
    scanSlaveEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_SLAVE_EDIT), instance_, nullptr);
    scanFunctionCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_FUNCTION_COMBO), instance_, nullptr);
    scanStartEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_START_EDIT), instance_, nullptr);
    scanEndEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"15", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_END_EDIT), instance_, nullptr);
    receiveLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RECEIVE_LOG), instance_, nullptr);
    statusText_ = CreateWindowExW(0, L"STATIC", L"未连接", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_STATUS_TEXT), instance_, nullptr);

    populateSerialOptionControls();
    setDefaultFonts();
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

    addComboItem(parityCombo_, L"无校验", static_cast<LPARAM>(SerialParity::None));
    addComboItem(parityCombo_, L"奇校验", static_cast<LPARAM>(SerialParity::Odd));
    addComboItem(parityCombo_, L"偶校验", static_cast<LPARAM>(SerialParity::Even));
    addComboItem(parityCombo_, L"标记校验", static_cast<LPARAM>(SerialParity::Mark));
    addComboItem(parityCombo_, L"空格校验", static_cast<LPARAM>(SerialParity::Space));
    selectComboData(parityCombo_, static_cast<LPARAM>(SerialParity::None));

    addComboItem(stopBitsCombo_, L"1", static_cast<LPARAM>(SerialStopBits::One));
    addComboItem(stopBitsCombo_, L"1.5", static_cast<LPARAM>(SerialStopBits::OnePointFive));
    addComboItem(stopBitsCombo_, L"2", static_cast<LPARAM>(SerialStopBits::Two));
    selectComboData(stopBitsCombo_, static_cast<LPARAM>(SerialStopBits::One));

    addComboItem(flowControlCombo_, L"无流控", static_cast<LPARAM>(SerialFlowControl::None));
    addComboItem(flowControlCombo_, L"RTS/CTS", static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts));
    addComboItem(flowControlCombo_, L"XON/XOFF", static_cast<LPARAM>(SerialFlowControl::SoftwareXonXoff));
    selectComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None));

    addComboItem(sendModeCombo_, L"文本", 0);
    addComboItem(sendModeCombo_, L"HEX", 1);
    selectComboData(sendModeCombo_, 0);

    addComboItem(lineEndingCombo_, L"无行尾", 0);
    addComboItem(lineEndingCombo_, L"CR", 1);
    addComboItem(lineEndingCombo_, L"LF", 2);
    addComboItem(lineEndingCombo_, L"CRLF", 3);
    selectComboData(lineEndingCombo_, 0);

    addComboItem(scanFunctionCombo_, L"FC03 保持寄存器", 3);
    addComboItem(scanFunctionCombo_, L"FC04 输入寄存器", 4);
    selectComboData(scanFunctionCombo_, 3);
}

void NativeMainWindow::layoutControls(int width, int height) {
    const int margin = 12;
    const int row = 30;
    const int gap = 8;
    const int labelWidth = 58;
    const int buttonWidth = 82;
    const int smallButtonWidth = 74;

    int x = margin;
    const int y = margin;
    MoveWindow(portLabel_, x, y + 6, labelWidth, 22, TRUE);
    x += labelWidth;
    MoveWindow(portCombo_, x, y, 170, 220, TRUE);
    x += 170 + gap;
    MoveWindow(refreshButton_, x, y, buttonWidth, row, TRUE);
    x += buttonWidth + gap;
    MoveWindow(saveProfileButton_, x, y, 96, row, TRUE);
    x += 96 + gap;
    MoveWindow(autoReconnectCheck_, x, y + 4, 96, 24, TRUE);
    x += 96 + gap;
    MoveWindow(connectButton_, x, y, buttonWidth, row, TRUE);
    x += buttonWidth + gap;
    MoveWindow(disconnectButton_, x, y, buttonWidth, row, TRUE);

    const int optionsY = y + row + 10;
    x = margin;
    MoveWindow(baudLabel_, x, optionsY + 6, labelWidth, 22, TRUE);
    x += labelWidth;
    MoveWindow(baudCombo_, x, optionsY, 104, 220, TRUE);
    x += 104 + gap;
    MoveWindow(dataBitsLabel_, x, optionsY + 6, labelWidth, 22, TRUE);
    x += labelWidth;
    MoveWindow(dataBitsCombo_, x, optionsY, 58, 160, TRUE);
    x += 58 + gap;
    MoveWindow(parityLabel_, x, optionsY + 6, 46, 22, TRUE);
    x += 46;
    MoveWindow(parityCombo_, x, optionsY, 90, 180, TRUE);
    x += 90 + gap;
    MoveWindow(stopBitsLabel_, x, optionsY + 6, labelWidth, 22, TRUE);
    x += labelWidth;
    MoveWindow(stopBitsCombo_, x, optionsY, 66, 160, TRUE);
    x += 66 + gap;
    MoveWindow(flowControlLabel_, x, optionsY + 6, 46, 22, TRUE);
    x += 46;
    MoveWindow(flowControlCombo_, x, optionsY, 100, 180, TRUE);
    x += 100 + gap;
    MoveWindow(dtrCheck_, x, optionsY + 4, 52, 24, TRUE);
    x += 52 + gap;
    MoveWindow(rtsCheck_, x, optionsY + 4, 52, 24, TRUE);

    const int sendY = optionsY + row + 10;
    x = margin;
    MoveWindow(sendModeCombo_, x, sendY, 72, 160, TRUE);
    x += 72 + gap;
    MoveWindow(lineEndingCombo_, x, sendY, 88, 160, TRUE);
    x += 88 + gap;
    MoveWindow(historyCombo_, x, sendY, 150, 200, TRUE);
    x += 150 + gap;
    const int trailingButtons = buttonWidth + smallButtonWidth * 3 + gap * 4;
    MoveWindow(sendEdit_, x, sendY, std::max(120, width - x - margin - trailingButtons), row, TRUE);
    x = width - margin - trailingButtons;
    MoveWindow(sendButton_, x, sendY, buttonWidth, row, TRUE);
    x += buttonWidth + gap;
    MoveWindow(pauseScrollButton_, x, sendY, smallButtonWidth, row, TRUE);
    x += smallButtonWidth + gap;
    MoveWindow(clearButton_, x, sendY, smallButtonWidth, row, TRUE);

    const int workflowY = sendY + row + 10;
    x = margin;
    MoveWindow(modbusButton_, x, workflowY, 104, row, TRUE);
    x += 104 + gap;
    MoveWindow(scanSlaveEdit_, x, workflowY, 42, row, TRUE);
    x += 42 + gap;
    MoveWindow(scanFunctionCombo_, x, workflowY, 140, 180, TRUE);
    x += 140 + gap;
    MoveWindow(scanStartEdit_, x, workflowY, 70, row, TRUE);
    x += 70 + gap;
    MoveWindow(scanEndEdit_, x, workflowY, 70, row, TRUE);
    x += 70 + gap * 2;
    MoveWindow(analysisButton_, x, workflowY, 104, row, TRUE);
    x += 104 + gap;
    MoveWindow(ruleVerifyButton_, x, workflowY, 90, row, TRUE);
    x += 90 + gap;
    MoveWindow(exportReportButton_, x, workflowY, 90, row, TRUE);

    const int logY = workflowY + row + 12;
    const int statusHeight = 26;
    MoveWindow(receiveLog_, margin, logY, width - margin * 2, height - logY - statusHeight - margin, TRUE);
    MoveWindow(statusText_, margin, height - statusHeight - 4, width - margin * 2, statusHeight, TRUE);
}

void NativeMainWindow::setDefaultFonts() {
    for (HWND child = GetWindow(window_, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        addControlFont(child, uiFont_);
    }
}

void NativeMainWindow::refreshPorts() {
    const std::wstring current = controlText(portCombo_);
    SendMessageW(portCombo_, CB_RESETCONTENT, 0, 0);
    const auto ports = Win32SerialEnumerator::availablePorts();
    for (const SerialPortDescriptor& port : ports) {
        SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8ToWide(port.portName).c_str()));
    }
    if (!ports.empty()) {
        const LRESULT preserved = SendMessageW(portCombo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(current.c_str()));
        SendMessageW(portCombo_, CB_SETCURSEL, preserved >= 0 ? static_cast<WPARAM>(preserved) : 0, 0);
        setStatus(L"已刷新串口列表，共 " + std::to_wstring(ports.size()) + L" 个。");
    } else {
        setStatus(L"未发现串口设备。");
    }
}

void NativeMainWindow::refreshSendHistory() {
    SendMessageW(historyCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(historyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"发送历史"));
    if (store_.isOpen()) {
        const auto history = store_.recentSendHistory(30);
        for (const native_storage::SendHistoryEntry& item : history) {
            SendMessageW(historyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8ToWide(item.content).c_str()));
        }
    }
    SendMessageW(historyCombo_, CB_SETCURSEL, 0, 0);
}

void NativeMainWindow::applySelectedHistory() {
    const LRESULT index = SendMessageW(historyCombo_, CB_GETCURSEL, 0, 0);
    if (index <= 0) {
        return;
    }
    setControlText(sendEdit_, controlText(historyCombo_));
}

void NativeMainWindow::applyLatestSerialProfile() {
    if (!store_.isOpen()) {
        return;
    }
    const auto profile = store_.latestSerialProfile();
    if (!profile.has_value()) {
        return;
    }

    const std::wstring portName = utf8ToWide(profile->portName);
    LRESULT portIndex = SendMessageW(portCombo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(portName.c_str()));
    if (portIndex < 0 && !portName.empty()) {
        portIndex = SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(portName.c_str()));
    }
    if (portIndex >= 0) {
        SendMessageW(portCombo_, CB_SETCURSEL, static_cast<WPARAM>(portIndex), 0);
    }

    setControlText(baudCombo_, std::to_wstring(profile->baudRate));
    selectComboData(dataBitsCombo_, profile->dataBits);
    selectComboData(parityCombo_, static_cast<LPARAM>(parityFromKey(profile->parity)));
    selectComboData(stopBitsCombo_, static_cast<LPARAM>(stopBitsFromKey(profile->stopBits)));
    selectComboData(flowControlCombo_, static_cast<LPARAM>(flowControlFromKey(profile->flowControl)));
    SendMessageW(dtrCheck_, BM_SETCHECK, profile->dataTerminalReady ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(rtsCheck_, BM_SETCHECK, profile->requestToSend ? BST_CHECKED : BST_UNCHECKED, 0);
    setStatus(L"已恢复默认串口配置：" + portName + L"。");
}

void NativeMainWindow::saveCurrentSerialProfile() {
    if (!store_.isOpen()) {
        setStatus(L"native 存储未打开，无法保存配置。");
        return;
    }
    const SerialOpenOptions options = currentOpenOptions();
    native_storage::SerialProfile profile;
    profile.name = "default";
    profile.portName = options.portName;
    profile.baudRate = options.baudRate;
    profile.dataBits = options.dataBits;
    profile.parity = parityKey(options.parity);
    profile.stopBits = stopBitsKey(options.stopBits);
    profile.flowControl = flowControlKey(options.flowControl);
    profile.dataTerminalReady = options.dataTerminalReady;
    profile.requestToSend = options.requestToSend;
    profile.updatedAtUtc = timestampText();
    if (!store_.saveSerialProfile(profile)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return;
    }
    setStatus(L"已保存默认串口配置：" + utf8ToWide(options.portName) + L"，" + std::to_wstring(options.baudRate) + L"。");
}

void NativeMainWindow::toggleConnection() {
    if (serialPort_.isOpen()) {
        disconnectSerial();
        return;
    }
    connectSerial();
}

void NativeMainWindow::connectSerial() {
    if (serialPort_.isOpen()) {
        setStatus(L"串口已经连接。");
        return;
    }

    SerialOpenOptions options = currentOpenOptions();
    const auto validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        setStatus(utf8ToWide(validation.errorMessage));
        return;
    }

    if (!serialPort_.open(options)) {
        setStatus(utf8ToWide(serialPort_.lastErrorText()));
        return;
    }

    lastOpenOptions_ = options;
    waitingReconnect_ = false;
    KillTimer(window_, IDT_RECONNECT);
    appendLog(L"[系统] 已连接 " + utf8ToWide(serialPort_.endpoint()));
    SetWindowTextW(connectButton_, L"断开");
    saveCurrentSerialProfile();
    setStatus(L"已连接。");
}

void NativeMainWindow::disconnectSerial() {
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::wstring endpoint = utf8ToWide(serialPort_.endpoint());
    serialPort_.close();
    appendLog(L"[系统] 已断开 " + endpoint);
    SetWindowTextW(connectButton_, L"连接");
    setStatus(L"已断开。");
}

void NativeMainWindow::sendPayload() {
    if (!serialPort_.isOpen()) {
        setStatus(L"串口未连接，无法发送。");
        return;
    }

    std::wstring errorText;
    const std::vector<std::uint8_t> payload = payloadFromInput(&errorText);
    if (!errorText.empty()) {
        setStatus(errorText);
        return;
    }
    if (payload.empty()) {
        setStatus(L"发送内容为空。");
        return;
    }

    const SerialIoResult result = serialPort_.writeBytes(payload);
    if (!result.ok) {
        setStatus(utf8ToWide(result.errorMessage));
        handleSerialFailure(result.errorMessage);
        return;
    }

    saveRawEvent("Tx", payload);
    if (store_.isOpen()) {
        native_storage::SendHistoryEntry history;
        history.content = wideToUtf8(controlText(sendEdit_));
        history.payloadMode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
        history.lineEnding = static_cast<int>(selectedComboData(lineEndingCombo_, 0));
        history.sentAtUtc = timestampText();
        store_.saveSendHistory(history);
        refreshSendHistory();
    }
    appendLog(L"[TX] " + bytesToHex(payload));
    setStatus(L"已发送 " + std::to_wstring(result.byteCount) + L" 字节。");
}

void NativeMainWindow::pollSerial() {
    if (!serialPort_.isOpen()) {
        return;
    }
    if (!serialPort_.waitForReadyRead(0)) {
        if (!serialPort_.lastErrorText().empty()) {
            handleSerialFailure(serialPort_.lastErrorText());
        }
        return;
    }

    for (int batch = 0; batch < 8; ++batch) {
        const std::vector<std::uint8_t> payload = serialPort_.readAvailable(4096);
        if (payload.empty()) {
            if (!serialPort_.lastErrorText().empty()) {
                handleSerialFailure(serialPort_.lastErrorText());
            }
            break;
        }
        saveRawEvent("Rx", payload);
        appendLog(L"[RX] " + bytesToHex(payload));
    }
}

void NativeMainWindow::handleSerialFailure(const std::string& message) {
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::string endpoint = serialPort_.endpoint();
    serialPort_.close();
    SetWindowTextW(connectButton_, L"连接");
    appendLog(L"[系统] 串口异常断开：" + utf8ToWide(message));
    const bool autoReconnect = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (autoReconnect && lastOpenOptions_.has_value()) {
        reconnectPortName_ = endpoint;
        waitingReconnect_ = true;
        SetTimer(window_, IDT_RECONNECT, 2000, nullptr);
        setStatus(L"串口异常断开，已进入自动重连等待：" + utf8ToWide(endpoint));
    } else {
        setStatus(utf8ToWide(message));
    }
}

void NativeMainWindow::tryAutoReconnect() {
    if (!waitingReconnect_ || !lastOpenOptions_.has_value() || serialPort_.isOpen()) {
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    const auto ports = Win32SerialEnumerator::availablePorts();
    const bool present = std::any_of(ports.begin(), ports.end(), [this](const SerialPortDescriptor& port) {
        return normalizedComPortName(port.portName) == normalizedComPortName(reconnectPortName_);
    });
    if (!present) {
        setStatus(L"等待自动重连：端口 " + utf8ToWide(reconnectPortName_) + L" 尚未恢复。");
        return;
    }

    SerialOpenOptions options = *lastOpenOptions_;
    options.portName = reconnectPortName_;
    if (!serialPort_.open(options)) {
        setStatus(L"自动重连失败：" + utf8ToWide(serialPort_.lastErrorText()));
        waitingReconnect_ = false;
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    waitingReconnect_ = false;
    KillTimer(window_, IDT_RECONNECT);
    SetWindowTextW(connectButton_, L"断开");
    appendLog(L"[系统] 自动重连成功 " + utf8ToWide(serialPort_.endpoint()));
    setStatus(L"自动重连成功。");
}

void NativeMainWindow::appendLog(const std::wstring& line) {
    if (scrollPaused_) {
        ++hiddenLogLineCount_;
        setStatus(L"滚动已暂停，已隐藏 " + std::to_wstring(hiddenLogLineCount_) + L" 条新日志；数据仍在接收和保存。");
        return;
    }
    if (GetWindowTextLengthW(receiveLog_) > static_cast<int>(kMaxLogChars)) {
        SetWindowTextW(receiveLog_, L"[系统] 接收日志已达到上限，已清空以保护长期运行内存。\r\n");
    }
    const std::wstring text = line + L"\r\n";
    SendMessageW(receiveLog_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(receiveLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void NativeMainWindow::setStatus(const std::wstring& text) {
    SetWindowTextW(statusText_, text.c_str());
}

std::wstring NativeMainWindow::controlText(HWND control) const {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void NativeMainWindow::setControlText(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

std::vector<std::uint8_t> NativeMainWindow::payloadFromInput(std::wstring* errorText) const {
    const std::wstring text = controlText(sendEdit_);
    const bool hexMode = selectedComboData(sendModeCombo_, 0) == 1;
    std::vector<std::uint8_t> payload;
    if (hexMode) {
        payload = parseHexPayload(text, errorText);
    } else {
        const std::string utf8 = wideToUtf8(text);
        payload.assign(utf8.begin(), utf8.end());
    }
    if (errorText == nullptr || errorText->empty()) {
        appendLineEnding(payload, static_cast<int>(selectedComboData(lineEndingCombo_, 0)));
    }
    return payload;
}

SerialOpenOptions NativeMainWindow::currentOpenOptions() const {
    SerialOpenOptions options;
    options.portName = wideToUtf8(controlText(portCombo_));
    options.baudRate = std::max(1, textToInt(baudCombo_, 115200));
    options.dataBits = static_cast<int>(selectedComboData(dataBitsCombo_, 8));
    options.parity = static_cast<SerialParity>(selectedComboData(parityCombo_, static_cast<LPARAM>(SerialParity::None)));
    options.stopBits = static_cast<SerialStopBits>(selectedComboData(stopBitsCombo_, static_cast<LPARAM>(SerialStopBits::One)));
    options.flowControl = static_cast<SerialFlowControl>(selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None)));
    options.dataTerminalReady = SendMessageW(dtrCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.requestToSend = SendMessageW(rtsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return options;
}

void NativeMainWindow::runModbusScan() {
    if (!serialPort_.isOpen()) {
        setStatus(L"请先连接串口，再执行 Modbus 扫描。");
        return;
    }
    if (!store_.isOpen()) {
        setStatus(L"native 存储未打开，无法保存 Modbus 扫描结果。");
        return;
    }

    core::modbus::ScanPlanOptions options;
    options.slaveId = textToInt(scanSlaveEdit_, 1);
    options.functionCode = static_cast<core::Byte>(selectedComboData(scanFunctionCombo_, 3));
    options.range.startAddress = textToInt(scanStartEdit_, 0);
    options.range.endAddress = textToInt(scanEndEdit_, 15);
    options.blockSize = 16;
    options.requestIntervalMs = 30;
    options.retryCount = 0;
    options.safetyLevel = core::modbus::ScanSafetyLevel::Custom;

    const auto planResult = core::modbus::buildScanPlan(options);
    if (!planResult.ok) {
        setStatus(utf8ToWide(planResult.errorMessage));
        MessageBoxW(window_, utf8ToWide(planResult.errorMessage).c_str(), L"Modbus 扫描参数无效", MB_ICONWARNING | MB_OK);
        return;
    }

    const std::string scanSessionId = "scan-" + timestampText();
    native_storage::ScanExecutionRecord execution;
    execution.session.sessionId = scanSessionId;
    execution.session.slaveId = planResult.plan.slaveId;
    execution.session.functionCode = planResult.plan.functionCode;
    execution.session.startAddress = planResult.plan.range.startAddress;
    execution.session.endAddress = planResult.plan.range.endAddress;
    execution.session.blockSize = planResult.plan.blockSize;
    execution.session.requestCount = planResult.plan.requestCount();
    execution.session.status = "running";
    execution.session.startedAtUtc = timestampText();

    appendLog(L"[系统] 开始 Modbus 扫描 " + utf8ToWide(scanSessionId));
    setStatus(L"正在执行 Modbus 扫描，请等待当前请求完成。");

    for (const core::modbus::ScanBlock& block : planResult.plan.blocks) {
        native_storage::ScanAttemptRecord attempt;
        attempt.sessionId = scanSessionId;
        attempt.blockIndex = block.index;
        attempt.attemptIndex = 0;
        attempt.startAddress = block.startAddress;
        attempt.quantity = block.quantity;
        attempt.requestFrame = block.requestFrame;
        attempt.sentAtUtc = timestampText();
        attempt.endpoint = serialPort_.endpoint();

        saveRawEvent("Tx", block.requestFrame);
        appendLog(L"[Modbus TX] " + bytesToHex(block.requestFrame));
        const SerialIoResult writeResult = serialPort_.writeBytes(block.requestFrame);
        if (!writeResult.ok) {
            attempt.status = "write-error";
            attempt.errorMessage = writeResult.errorMessage;
            execution.attempts.push_back(std::move(attempt));
            ++execution.session.failedBlockCount;
            handleSerialFailure(writeResult.errorMessage);
            break;
        }

        std::vector<std::uint8_t> response;
        const std::size_t expectedNormalBytes = static_cast<std::size_t>(5 + block.quantity * 2);
        const ULONGLONG deadline = GetTickCount64() + 1200;
        while (GetTickCount64() < deadline) {
            if (serialPort_.waitForReadyRead(50)) {
                std::vector<std::uint8_t> chunk = serialPort_.readAvailable(260);
                response.insert(response.end(), chunk.begin(), chunk.end());
                if (response.size() >= expectedNormalBytes
                    || (response.size() >= 5 && (response[1] & 0x80U) != 0)) {
                    break;
                }
            } else if (!serialPort_.lastErrorText().empty()) {
                attempt.errorMessage = serialPort_.lastErrorText();
                break;
            }
        }

        attempt.receivedAtUtc = timestampText();
        attempt.responseFrame = response;
        if (!response.empty()) {
            saveRawEvent("Rx", response);
            appendLog(L"[Modbus RX] " + bytesToHex(response));
        }

        if (response.empty()) {
            attempt.status = "timeout";
            if (attempt.errorMessage.empty()) {
                attempt.errorMessage = "等待 Modbus 响应超时。";
            }
            ++execution.session.failedBlockCount;
            execution.attempts.push_back(std::move(attempt));
            continue;
        }

        const auto parsed = core::modbus::parseReadResponse(
            response,
            planResult.plan.slaveId,
            planResult.plan.functionCode,
            block.startAddress,
            block.quantity);
        if (!parsed.ok) {
            attempt.status = "parse-error";
            attempt.errorMessage = parsed.errorMessage;
            attempt.isModbusException = parsed.isException;
            attempt.exceptionCode = parsed.exceptionCode;
            attempt.exceptionDescription = parsed.exceptionDescription;
            ++execution.session.failedBlockCount;
            execution.attempts.push_back(std::move(attempt));
            continue;
        }

        attempt.status = "success";
        ++execution.session.successBlockCount;
        for (const core::modbus::RegisterObservation& observation : parsed.observations) {
            native_storage::ScanObservationRecord record;
            record.sessionId = scanSessionId;
            record.blockIndex = block.index;
            record.attemptIndex = 0;
            record.slaveId = parsed.slaveId;
            record.functionCode = parsed.functionCode;
            record.address = observation.address;
            record.value = observation.value;
            record.observedAtUtc = attempt.receivedAtUtc;
            execution.observations.push_back(std::move(record));
        }
        execution.attempts.push_back(std::move(attempt));
        if (planResult.plan.requestIntervalMs > 0) {
            Sleep(static_cast<DWORD>(planResult.plan.requestIntervalMs));
        }
    }

    execution.session.finishedAtUtc = timestampText();
    execution.session.status = execution.session.failedBlockCount == 0 ? "completed" : (execution.session.successBlockCount > 0 ? "partial" : "failed");
    if (!store_.saveScanExecution(execution)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return;
    }

    const std::wstring summary = L"Modbus 扫描已保存：成功块 "
        + std::to_wstring(execution.session.successBlockCount)
        + L"，失败块 "
        + std::to_wstring(execution.session.failedBlockCount)
        + L"，观测 "
        + std::to_wstring(execution.observations.size())
        + L"。";
    appendLog(L"[系统] " + summary);
    setStatus(summary);
}

void NativeMainWindow::showAnalysisWorkspace() {
    if (!store_.isOpen()) {
        showDeferredFeature(L"分析工作区", L"native 存储未打开，无法读取扫描和候选数据。");
        return;
    }
    const std::wstring message =
        L"Win32 native 分析工作区入口已接入。\n\n"
        L"当前可用：Modbus 扫描结果会保存到 native 存储；后续候选生成、稳定性分析和规则编辑会继续接入这里。\n\n"
        L"请先用“Modbus 扫描”积累扫描数据。此入口不会静默失败，缺失能力会在后续版本继续补齐。";
    MessageBoxW(window_, message.c_str(), L"分析工作区", MB_ICONINFORMATION | MB_OK);
    setStatus(L"已打开分析工作区说明：候选生成和稳定性分析仍需继续补齐。");
}

void NativeMainWindow::showRuleVerification() {
    showDeferredFeature(
        L"规则验证",
        L"规则验证菜单已恢复，但完整验证 UI 还需要先补齐 native 候选分析和规则编辑。\n\n"
        L"当前 native 包不会再隐藏该缺口；Qt baseline 仍是完整规则验证路径。");
}

void NativeMainWindow::exportReport() {
    showDeferredFeature(
        L"导出验证报告",
        L"报告渲染核心已是 Qt-free，但 Win32 native 还缺最近规则验证运行选择和文件保存 UI。\n\n"
        L"因此本入口暂不导出空报告，避免误导测试结果。");
}

void NativeMainWindow::showAbout() {
    MessageBoxW(
        window_,
        L"串口值匹配器 Win32 Native\n\n作者：w\n面向中文用户的 Windows 原生串口调试、Modbus 扫描和值候选分析工具。\n\n当前目标：不依赖 C#/.NET/Qt 运行库，并逐步补齐 Qt baseline 功能。",
        L"关于串口值匹配器",
        MB_ICONINFORMATION | MB_OK);
}

void NativeMainWindow::showDeferredFeature(const std::wstring& title, const std::wstring& message) {
    MessageBoxW(window_, message.c_str(), title.c_str(), MB_ICONINFORMATION | MB_OK);
    setStatus(title + L"：该 native 功能入口已恢复，但完整交互仍在补齐。");
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
    event.timestampUtc = timestampText();
    event.endpoint = serialPort_.endpoint();
    event.payload = payload;
    store_.appendRawEvent(event);
}

} // namespace svm::win32

#endif
