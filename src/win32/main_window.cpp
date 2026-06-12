#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/resource.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_enumerator.h"
#include "win32/win32_serial_types.h"

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
        900,
        620,
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
    const bool ok = store.appendRawEvent(event) && store.rawEventCount() == 1;
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
        createControls();
        refreshPorts();
        store_.open(defaultStoreDirectory());
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
            connectSerial();
            return 0;
        case IDC_DISCONNECT_BUTTON:
            disconnectSerial();
            return 0;
        case IDC_SEND_BUTTON:
            sendPayload();
            return 0;
        case IDC_CLEAR_BUTTON:
            SetWindowTextW(receiveLog_, L"");
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
        break;
    case WM_DESTROY:
        KillTimer(window_, IDT_SERIAL_POLL);
        disconnectSerial();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void NativeMainWindow::createControls() {
    uiFont_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    portLabel_ = CreateWindowExW(0, L"STATIC", L"串口", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    portCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PORT_COMBO), instance_, nullptr);
    refreshButton_ = CreateWindowExW(0, L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_REFRESH_BUTTON), instance_, nullptr);
    baudLabel_ = CreateWindowExW(0, L"STATIC", L"波特率", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    baudEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"115200", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_BAUD_EDIT), instance_, nullptr);
    connectButton_ = CreateWindowExW(0, L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CONNECT_BUTTON), instance_, nullptr);
    disconnectButton_ = CreateWindowExW(0, L"BUTTON", L"断开", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_DISCONNECT_BUTTON), instance_, nullptr);
    hexCheck_ = CreateWindowExW(0, L"BUTTON", L"HEX", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_HEX_CHECK), instance_, nullptr);
    sendEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_EDIT), instance_, nullptr);
    sendButton_ = CreateWindowExW(0, L"BUTTON", L"发送", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_BUTTON), instance_, nullptr);
    clearButton_ = CreateWindowExW(0, L"BUTTON", L"清空", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CLEAR_BUTTON), instance_, nullptr);
    receiveLog_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RECEIVE_LOG), instance_, nullptr);
    statusText_ = CreateWindowExW(0, L"STATIC", L"未连接", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_STATUS_TEXT), instance_, nullptr);

    setDefaultFonts();
}

void NativeMainWindow::layoutControls(int width, int height) {
    const int margin = 12;
    const int row = 30;
    const int labelWidth = 54;
    const int portWidth = 150;
    const int buttonWidth = 72;
    const int baudWidth = 92;

    int x = margin;
    const int y = margin;
    MoveWindow(portLabel_, x, y + 6, labelWidth, 22, TRUE);
    x += labelWidth;
    MoveWindow(portCombo_, x, y, portWidth, 200, TRUE);
    x += portWidth + 8;
    MoveWindow(refreshButton_, x, y, buttonWidth, row, TRUE);
    x += buttonWidth + 14;
    MoveWindow(baudLabel_, x, y + 6, labelWidth, 22, TRUE);
    x += labelWidth;
    MoveWindow(baudEdit_, x, y, baudWidth, row, TRUE);
    x += baudWidth + 8;
    MoveWindow(connectButton_, x, y, buttonWidth, row, TRUE);
    x += buttonWidth + 8;
    MoveWindow(disconnectButton_, x, y, buttonWidth, row, TRUE);

    const int sendY = y + row + 12;
    MoveWindow(hexCheck_, margin, sendY + 5, 56, row, TRUE);
    MoveWindow(sendEdit_, margin + 60, sendY, width - margin * 2 - 60 - buttonWidth * 2 - 16, row, TRUE);
    MoveWindow(sendButton_, width - margin - buttonWidth * 2 - 8, sendY, buttonWidth, row, TRUE);
    MoveWindow(clearButton_, width - margin - buttonWidth, sendY, buttonWidth, row, TRUE);

    const int logY = sendY + row + 12;
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
    SendMessageW(portCombo_, CB_RESETCONTENT, 0, 0);
    const auto ports = Win32SerialEnumerator::availablePorts();
    for (const SerialPortDescriptor& port : ports) {
        SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8ToWide(port.portName).c_str()));
    }
    if (!ports.empty()) {
        SendMessageW(portCombo_, CB_SETCURSEL, 0, 0);
        setStatus(L"已刷新串口列表。");
    } else {
        setStatus(L"未发现串口设备。");
    }
}

void NativeMainWindow::connectSerial() {
    if (serialPort_.isOpen()) {
        setStatus(L"串口已经连接。");
        return;
    }

    SerialOpenOptions options;
    options.portName = wideToUtf8(controlText(portCombo_));
    options.baudRate = std::max(1, _wtoi(controlText(baudEdit_).c_str()));
    const auto validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        setStatus(utf8ToWide(validation.errorMessage));
        return;
    }

    if (!serialPort_.open(options)) {
        setStatus(utf8ToWide(serialPort_.lastErrorText()));
        return;
    }

    appendLog(L"[系统] 已连接 " + utf8ToWide(serialPort_.endpoint()));
    setStatus(L"已连接。");
}

void NativeMainWindow::disconnectSerial() {
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::wstring endpoint = utf8ToWide(serialPort_.endpoint());
    serialPort_.close();
    appendLog(L"[系统] 已断开 " + endpoint);
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
        return;
    }

    saveRawEvent("Tx", payload);
    appendLog(L"[TX] " + bytesToHex(payload));
    setStatus(L"已发送 " + std::to_wstring(result.byteCount) + L" 字节。");
}

void NativeMainWindow::pollSerial() {
    if (!serialPort_.isOpen() || !serialPort_.waitForReadyRead(0)) {
        return;
    }

    for (int batch = 0; batch < 8; ++batch) {
        const std::vector<std::uint8_t> payload = serialPort_.readAvailable(4096);
        if (payload.empty()) {
            break;
        }
        saveRawEvent("Rx", payload);
        appendLog(L"[RX] " + bytesToHex(payload));
    }
}

void NativeMainWindow::appendLog(const std::wstring& line) {
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
    const bool hexMode = SendMessageW(hexCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (hexMode) {
        return parseHexPayload(text, errorText);
    }
    const std::string utf8 = wideToUtf8(text);
    return std::vector<std::uint8_t>(utf8.begin(), utf8.end());
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
