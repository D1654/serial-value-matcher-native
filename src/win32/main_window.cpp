#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_enumerator.h"
#include "win32/win32_serial_types.h"

#include "core/analysis_core.h"
#include "core/modbus_core.h"
#include "core/report_core.h"

#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

namespace analysis_core = ::svm::core::analysis;
namespace modbus_core = ::svm::core::modbus;
namespace report_core = ::svm::core::report;

constexpr wchar_t kWindowClassName[] = L"SvmNativeMainWindow";
constexpr std::size_t kMaxLogEntries = 2000;
constexpr std::size_t kMaxVisibleLogChars = 350000;
constexpr std::size_t kMaxRenderedLogLineChars = 4096;
constexpr UINT kCodePageGbk = 936;
constexpr UINT kCodePageAscii = 20127;

const wchar_t* tx(T id) {
    return uiText(id);
}

struct NativeLogPalette {
    COLORREF background = RGB(255, 255, 255);
    COLORREF normal = RGB(32, 32, 32);
    COLORREF system = RGB(84, 84, 84);
    COLORREF tx = RGB(0, 91, 170);
    COLORREF rx = RGB(0, 128, 72);
    COLORREF modbusTx = RGB(138, 82, 0);
    COLORREF modbusRx = RGB(108, 72, 145);
    COLORREF error = RGB(176, 38, 38);
};

NativeLogPalette logPalette(int themeIndex) {
    switch (themeIndex) {
    case 1:
        return {
            RGB(250, 250, 248),
            RGB(32, 32, 32),
            RGB(88, 88, 88),
            RGB(35, 95, 154),
            RGB(35, 120, 84),
            RGB(130, 86, 36),
            RGB(106, 84, 150),
            RGB(170, 48, 48),
        };
    case 2:
        return {
            RGB(12, 12, 12),
            RGB(232, 232, 232),
            RGB(210, 210, 210),
            RGB(70, 190, 255),
            RGB(95, 230, 130),
            RGB(255, 210, 72),
            RGB(215, 145, 255),
            RGB(255, 88, 88),
        };
    default:
        return {};
    }
}

COLORREF logColorForKind(NativeLogKind kind, int themeIndex) {
    const NativeLogPalette palette = logPalette(themeIndex);
    switch (kind) {
    case NativeLogKind::System:
        return palette.system;
    case NativeLogKind::Tx:
        return palette.tx;
    case NativeLogKind::Rx:
        return palette.rx;
    case NativeLogKind::ModbusTx:
        return palette.modbusTx;
    case NativeLogKind::ModbusRx:
        return palette.modbusRx;
    case NativeLogKind::Error:
        return palette.error;
    }
    return palette.normal;
}

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
                *errorText = tx(T::HexInvalidChar);
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
            *errorText = tx(T::HexOddNibble);
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

std::vector<std::wstring> splitByteTokens(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t ch : text) {
        if (std::iswspace(ch) || ch == L',' || ch == L';' || ch == L'\uFF0C' || ch == L'\u3001') {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

std::vector<std::uint8_t> parseDecimalPayload(std::wstring_view text, std::wstring* errorText) {
    const std::vector<std::wstring> tokens = splitByteTokens(text);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(tokens.size());
    for (const std::wstring& token : tokens) {
        wchar_t* end = nullptr;
        const long value = std::wcstol(token.c_str(), &end, 10);
        if (end == token.c_str() || *end != L'\0' || value < 0 || value > 255) {
            if (errorText != nullptr) {
                *errorText = tx(T::SendModeInvalidDecimal);
            }
            return {};
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

std::vector<std::uint8_t> parseBinaryPayload(std::wstring_view text, std::wstring* errorText) {
    std::wstring bits;
    for (wchar_t ch : text) {
        if (std::iswspace(ch) || ch == L',' || ch == L';' || ch == L'\uFF0C' || ch == L'\u3001') {
            continue;
        }
        if (ch != L'0' && ch != L'1') {
            if (errorText != nullptr) {
                *errorText = tx(T::SendModeInvalidBinary);
            }
            return {};
        }
        bits.push_back(ch);
    }

    if (bits.empty()) {
        return {};
    }
    if ((bits.size() % 8) != 0) {
        if (errorText != nullptr) {
            *errorText = tx(T::SendModeInvalidBinary);
        }
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(bits.size() / 8);
    for (std::size_t index = 0; index < bits.size(); index += 8) {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 8; ++bit) {
            value = static_cast<std::uint8_t>((value << 1U) | (bits[index + bit] == L'1' ? 1U : 0U));
        }
        bytes.push_back(value);
    }
    return bytes;
}

std::vector<std::uint8_t> encodeTextPayload(std::wstring_view text, UINT codePage, std::wstring* errorText) {
    if (text.empty()) {
        return {};
    }

    if (codePage == kCodePageAscii) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size());
        for (wchar_t ch : text) {
            if (ch > 0x7F) {
                if (errorText != nullptr) {
                    *errorText = tx(T::TextEncodingFailed);
                }
                return {};
            }
            bytes.push_back(static_cast<std::uint8_t>(ch));
        }
        return bytes;
    }

    BOOL usedDefaultChar = FALSE;
    BOOL* usedDefaultCharPtr = codePage == CP_UTF8 ? nullptr : &usedDefaultChar;
    const int required = WideCharToMultiByte(
        codePage,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        usedDefaultCharPtr);
    if (required <= 0) {
        if (errorText != nullptr) {
            *errorText = tx(T::TextEncodingFailed);
        }
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(required));
    usedDefaultChar = FALSE;
    if (WideCharToMultiByte(
            codePage,
            0,
            text.data(),
            static_cast<int>(text.size()),
            reinterpret_cast<char*>(bytes.data()),
            required,
            nullptr,
            usedDefaultCharPtr) <= 0
        || usedDefaultChar) {
        if (errorText != nullptr) {
            *errorText = tx(T::TextEncodingFailed);
        }
        return {};
    }
    return bytes;
}

std::wstring bytesToDecimal(const std::vector<std::uint8_t>& bytes) {
    std::wostringstream output;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << L' ';
        }
        output << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::wstring bytesToBinary(const std::vector<std::uint8_t>& bytes) {
    std::wostringstream output;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << L' ';
        }
        for (int bit = 7; bit >= 0; --bit) {
            output << (((bytes[index] >> bit) & 1U) != 0 ? L'1' : L'0');
        }
    }
    return output.str();
}

std::wstring decodeBytesToText(const std::vector<std::uint8_t>& bytes, UINT codePage) {
    if (bytes.empty()) {
        return {};
    }

    if (codePage == kCodePageAscii) {
        std::wstring text;
        text.reserve(bytes.size());
        for (std::uint8_t byte : bytes) {
            text.push_back(byte <= 0x7F ? static_cast<wchar_t>(byte) : L'.');
        }
        return text;
    }

    const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const int required = MultiByteToWideChar(
        codePage,
        flags,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        nullptr,
        0);
    if (required <= 0) {
        std::wstring text;
        text.reserve(bytes.size());
        for (std::uint8_t byte : bytes) {
            text.push_back(byte >= 0x20 && byte <= 0x7E ? static_cast<wchar_t>(byte) : L'.');
        }
        return text;
    }

    std::wstring text(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        text.data(),
        required);
    return text;
}

std::wstring sanitizeLogText(std::wstring_view text) {
    std::wstring sanitized;
    sanitized.reserve(text.size());
    for (wchar_t ch : text) {
        switch (ch) {
        case L'\r':
            sanitized.append(L"\\r");
            break;
        case L'\n':
            sanitized.append(L"\\n");
            break;
        case L'\t':
            sanitized.append(L"\\t");
            break;
        default:
            sanitized.push_back(ch < 0x20 ? L'.' : ch);
            break;
        }
    }
    return sanitized;
}

std::wstring lowerCopy(std::wstring_view text) {
    std::wstring lowered;
    lowered.reserve(text.size());
    for (wchar_t ch : text) {
        lowered.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return lowered;
}

bool containsCaseInsensitive(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) {
        return true;
    }
    return lowerCopy(haystack).find(lowerCopy(needle)) != std::wstring::npos;
}

std::wstring clipRenderedLogLine(std::wstring text) {
    if (text.size() <= kMaxRenderedLogLineChars) {
        return text;
    }
    const std::wstring suffix = tx(T::LogEntryClippedSuffix);
    const std::size_t keep = kMaxRenderedLogLineChars > suffix.size()
        ? kMaxRenderedLogLineChars - suffix.size()
        : kMaxRenderedLogLineChars;
    text.resize(keep);
    text += suffix;
    return text;
}

std::wstring localClockText() {
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t buffer[32] = {};
    swprintf_s(
        buffer,
        L"%02u:%02u:%02u.%03u",
        static_cast<unsigned int>(now.wHour),
        static_cast<unsigned int>(now.wMinute),
        static_cast<unsigned int>(now.wSecond),
        static_cast<unsigned int>(now.wMilliseconds));
    return buffer;
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

double textToDouble(HWND control, double fallback, bool* ok = nullptr) {
    if (ok != nullptr) {
        *ok = false;
    }
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return fallback;
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str()) {
        return fallback;
    }
    while (end != nullptr && *end != L'\0' && std::iswspace(*end)) {
        ++end;
    }
    if (end != nullptr && *end == L'\0') {
        if (ok != nullptr) {
            *ok = true;
        }
        return value;
    }
    return fallback;
}

std::string timestampIdText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm utc = {};
    gmtime_s(&utc, &time);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d%H%M%S", &utc);
    char suffix[8] = {};
    std::snprintf(suffix, sizeof(suffix), "%03lld", static_cast<long long>(millis));
    return std::string(buffer) + suffix;
}

std::string formatNumber(double value) {
    std::ostringstream output;
    output.precision(12);
    output << value;
    return output.str();
}

analysis_core::RegisterSample sampleFromObservation(const native_storage::ScanObservationRecord& observation) {
    analysis_core::RegisterSample sample;
    sample.observationId = observation.id;
    sample.sessionId = observation.sessionId;
    sample.slaveId = observation.slaveId;
    sample.functionCode = observation.functionCode;
    sample.address = observation.address;
    sample.value = static_cast<std::uint16_t>(observation.value);
    sample.blockIndex = observation.blockIndex;
    sample.attemptIndex = observation.attemptIndex;
    return sample;
}

native_storage::MatchCandidateRecord candidateRecordFromCore(
    const analysis_core::ValueMatchCandidate& candidate,
    const std::string& runId,
    int rankIndex,
    const std::string& observedAtUtc) {
    native_storage::MatchCandidateRecord record;
    record.runId = runId;
    record.rankIndex = rankIndex;
    record.candidateType = analysis_core::numericCandidateTypeName(candidate.type);
    record.wordOrder = analysis_core::wordOrderName(candidate.wordOrder);
    record.byteOrder = analysis_core::byteOrderName(candidate.byteOrder);
    record.sourceSessionId = candidate.sessionId;
    record.slaveId = candidate.slaveId;
    record.functionCode = candidate.functionCode;
    record.startAddress = candidate.startAddress;
    record.registerCount = candidate.registerCount;
    record.observationIds = candidate.observationIds;
    record.addresses = candidate.addresses;
    record.blockIndexes = candidate.blockIndexes;
    record.attemptIndexes = candidate.attemptIndexes;
    record.rawRegisters.reserve(candidate.rawRegisters.size());
    for (std::uint16_t value : candidate.rawRegisters) {
        record.rawRegisters.push_back(static_cast<int>(value));
    }
    record.decodedValue = candidate.decodedValue;
    record.scaleMultiplier = candidate.scale.multiplier;
    record.scaleOffset = candidate.scale.offset;
    record.engineeringValue = candidate.engineeringValue;
    record.delta = candidate.delta;
    record.absoluteError = candidate.absoluteError;
    record.effectiveTolerance = candidate.effectiveTolerance;
    record.score = candidate.score;
    record.observedAtUtc = observedAtUtc;
    record.evidenceText = candidate.evidenceText;
    return record;
}

std::wstring candidateDisplayText(const native_storage::MatchCandidateRecord& candidate) {
    std::wostringstream output;
    output << L"#" << (candidate.rankIndex + 1)
           << L" " << utf8ToWide(candidate.candidateType)
           << L" @" << candidate.startAddress
           << L"=" << utf8ToWide(formatNumber(candidate.engineeringValue))
           << L" score " << utf8ToWide(formatNumber(candidate.score));
    return output.str();
}

std::wstring portDisplayText(const SerialPortDescriptor& port) {
    const std::wstring portName = utf8ToWide(port.portName);
    const std::wstring description = sanitizeLogText(utf8ToWide(port.description));
    if (description.empty() || description == portName) {
        return portName;
    }
    return portName + L" - " + description;
}

std::wstring ruleDisplayName(const native_storage::MatchCandidateRecord& candidate, const std::wstring& targetName) {
    if (!targetName.empty()) {
        return targetName;
    }
    return utf8ToWide(candidate.candidateType) + L"@" + std::to_wstring(candidate.startAddress);
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
        tx(T::WindowTitle),
        WS_OVERLAPPEDWINDOW,
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
    history.content = "AT+\xE6\xB5\x8B\xE8\xAF\x95";
    history.payloadMode = 0;
    history.lineEnding = 0;
    history.textEncodingCodePage = CP_UTF8;
    history.sentAtUtc = timestampText();

    native_storage::UiPreferences preferences;
    preferences.logThemeIndex = 1;
    preferences.logFormat = 4;
    preferences.logEncodingCodePage = CP_UTF8;
    preferences.showLogTimestamps = false;
    preferences.updatedAtUtc = timestampText();

    const bool ok = store.appendRawEvent(event)
        && store.rawEventCount() == 1
        && store.saveSerialProfile(profile)
        && store.latestSerialProfile().has_value()
        && store.saveSendHistory(history)
        && !store.recentSendHistory(1).empty()
        && store.saveUiPreferences(preferences)
        && store.latestUiPreferences().has_value();
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
    case WM_GETMINMAXINFO: {
        auto* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
        minMax->ptMinTrackSize.x = 1080;
        minMax->ptMinTrackSize.y = 760;
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
            latestMatchRunId_ = run->runId;
            refreshCandidateCombo(run->runId);
        }
        if (const auto verificationRun = store_.latestRuleVerificationRun(); verificationRun.has_value()) {
            latestVerificationRunId_ = verificationRun->verificationRunId;
        }
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
        case IDC_LOG_FILTER_EDIT:
            if (HIWORD(wParam) == EN_CHANGE) {
                updateLogFilter();
            }
            return 0;
        case IDC_LOG_SEARCH_EDIT:
            if (HIWORD(wParam) == EN_CHANGE) {
                lastLogSearchText_.clear();
                lastLogSearchOffset_ = 0;
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
            scrollPaused_ = !scrollPaused_;
            SetWindowTextW(pauseScrollButton_, scrollPaused_ ? tx(T::ResumeScrollButton) : tx(T::PauseScrollButton));
            if (!scrollPaused_) {
                rebuildLogView();
                hiddenLogLineCount_ = 0;
            }
            setStatus(scrollPaused_ ? tx(T::PauseScrollStatus) : tx(T::ResumeScrollStatus));
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
            scrollPaused_ = !scrollPaused_;
            SetWindowTextW(pauseScrollButton_, scrollPaused_ ? tx(T::ResumeScrollButton) : tx(T::PauseScrollButton));
            if (!scrollPaused_) {
                rebuildLogView();
                hiddenLogLineCount_ = 0;
            }
            setStatus(scrollPaused_ ? tx(T::PauseScrollStatus) : tx(T::ResumeScrollStatus));
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
        saveUiPreferences();
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        saveUiPreferences();
        KillTimer(window_, IDT_SERIAL_POLL);
        KillTimer(window_, IDT_RECONNECT);
        disconnectSerial();
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
    uiFont_ = CreateFontW(
        -14,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei UI");
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
    sendModeCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_MODE_COMBO), instance_, nullptr);
    textEncodingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TEXT_ENCODING_COMBO), instance_, nullptr);
    lineEndingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LINE_ENDING_COMBO), instance_, nullptr);
    logFormatCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_FORMAT_COMBO), instance_, nullptr);
    logEncodingCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_ENCODING_COMBO), instance_, nullptr);
    logFilterEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_FILTER_EDIT), instance_, nullptr);
    logSearchEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_SEARCH_EDIT), instance_, nullptr);
    findLogButton_ = CreateWindowExW(0, L"BUTTON", tx(T::FindButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_LOG_FIND_BUTTON), instance_, nullptr);
    copyLogButton_ = CreateWindowExW(0, L"BUTTON", tx(T::CopyLogButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_COPY_LOG_BUTTON), instance_, nullptr);
    exportLogButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ExportLogButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_EXPORT_LOG_BUTTON), instance_, nullptr);
    historyCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_HISTORY_COMBO), instance_, nullptr);
    sendEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_EDIT), instance_, nullptr);
    sendButton_ = CreateWindowExW(0, L"BUTTON", tx(T::SendButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SEND_BUTTON), instance_, nullptr);
    pauseScrollButton_ = CreateWindowExW(0, L"BUTTON", tx(T::PauseScrollButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_PAUSE_SCROLL_BUTTON), instance_, nullptr);
    clearButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ClearButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_CLEAR_BUTTON), instance_, nullptr);
    modbusButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ModbusScanButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_MODBUS_BUTTON), instance_, nullptr);
    analysisButton_ = CreateWindowExW(0, L"BUTTON", tx(T::AnalysisButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_ANALYSIS_BUTTON), instance_, nullptr);
    ruleVerifyButton_ = CreateWindowExW(0, L"BUTTON", tx(T::RuleVerifyButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_RULE_VERIFY_BUTTON), instance_, nullptr);
    exportReportButton_ = CreateWindowExW(0, L"BUTTON", tx(T::ExportReportButton), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_EXPORT_REPORT_BUTTON), instance_, nullptr);
    scanSectionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanSectionLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanSlaveLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanSlaveLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanSlaveEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_SLAVE_EDIT), instance_, nullptr);
    scanFunctionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanFunctionLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanFunctionCombo_ = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_FUNCTION_COMBO), instance_, nullptr);
    scanStartLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanStartLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanStartEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_START_EDIT), instance_, nullptr);
    scanEndLabel_ = CreateWindowExW(0, L"STATIC", tx(T::ScanEndLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    scanEndEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"15", WS_CHILD | WS_VISIBLE | ES_NUMBER, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_SCAN_END_EDIT), instance_, nullptr);
    analysisSectionLabel_ = CreateWindowExW(0, L"STATIC", tx(T::AnalysisSectionLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetStatic_ = CreateWindowExW(0, L"STATIC", tx(T::TargetNameLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetLabelEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", tx(T::TargetNameDefault), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TARGET_LABEL_EDIT), instance_, nullptr);
    targetValueStatic_ = CreateWindowExW(0, L"STATIC", tx(T::TargetValueLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetValueEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", tx(T::TargetValueDefault), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TARGET_VALUE_EDIT), instance_, nullptr);
    targetUnitStatic_ = CreateWindowExW(0, L"STATIC", tx(T::TargetUnitLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    targetUnitEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", tx(T::TargetUnitDefault), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TARGET_UNIT_EDIT), instance_, nullptr);
    toleranceStatic_ = CreateWindowExW(0, L"STATIC", tx(T::ToleranceFieldLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    toleranceEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", tx(T::ToleranceDefault), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, window_, reinterpret_cast<HMENU>(IDC_TOLERANCE_EDIT), instance_, nullptr);
    candidateStatic_ = CreateWindowExW(0, L"STATIC", tx(T::CandidateLabel), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
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
    SendMessageW(logFilterEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::LogFilterCue)));
    SendMessageW(logSearchEdit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tx(T::LogSearchCue)));
    SendMessageW(receiveLog_, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(kMaxVisibleLogChars + kMaxRenderedLogLineChars));

    populateSerialOptionControls();
    setDefaultFonts();
    applyLogTheme(0);
    updateRtsControlState();
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
    addComboItem(textEncodingCombo_, tx(T::GbkEncoding), kCodePageGbk);
    addComboItem(textEncodingCombo_, tx(T::AnsiEncoding), CP_ACP);
    addComboItem(textEncodingCombo_, tx(T::AsciiEncoding), kCodePageAscii);
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
    addComboItem(logEncodingCombo_, tx(T::GbkEncoding), kCodePageGbk);
    addComboItem(logEncodingCombo_, tx(T::AnsiEncoding), CP_ACP);
    addComboItem(logEncodingCombo_, tx(T::AsciiEncoding), kCodePageAscii);
    selectComboData(logEncodingCombo_, CP_UTF8);

    addComboItem(scanFunctionCombo_, tx(T::Fc03Holding), 3);
    addComboItem(scanFunctionCombo_, tx(T::Fc04Input), 4);
    selectComboData(scanFunctionCombo_, 3);

    addComboItem(candidateCombo_, tx(T::CandidatePlaceholder), 0);
    selectComboData(candidateCombo_, 0);
}

void NativeMainWindow::layoutControls(int width, int height) {
    width = std::max(width, 1040);
    height = std::max(height, 700);

    const int margin = 12;
    const int groupPad = 16;
    const int row = 30;
    const int gap = 8;
    const int labelHeight = 22;
    const int labelWidth = 56;
    const int buttonWidth = 82;
    const int smallButtonWidth = 74;

    const int connectionX = margin;
    const int connectionY = margin;
    const int connectionWidth = width - margin * 2;
    const int connectionHeight = 122;
    MoveWindow(connectionGroup_, connectionX, connectionY, connectionWidth, connectionHeight, TRUE);

    int x = connectionX + groupPad;
    int y = connectionY + 26;
    const int actionsWidth = buttonWidth * 2 + gap;
    const int actionsX = connectionX + connectionWidth - groupPad - actionsWidth;
    MoveWindow(portLabel_, x, y + 6, labelWidth, labelHeight, TRUE);
    x += labelWidth;
    MoveWindow(portCombo_, x, y, 360, 260, TRUE);
    x += 360 + gap;
    MoveWindow(refreshButton_, x, y, smallButtonWidth, row, TRUE);
    x += smallButtonWidth + gap;
    MoveWindow(saveProfileButton_, x, y, 96, row, TRUE);
    x += 96 + gap;
    MoveWindow(autoReconnectCheck_, x, y + 4, 116, 24, TRUE);
    MoveWindow(connectButton_, actionsX, y, buttonWidth, row, TRUE);
    MoveWindow(disconnectButton_, actionsX + buttonWidth + gap, y, buttonWidth, row, TRUE);

    y = connectionY + 70;
    x = connectionX + groupPad;
    MoveWindow(baudLabel_, x, y + 6, labelWidth, labelHeight, TRUE);
    x += labelWidth;
    MoveWindow(baudCombo_, x, y, 104, 220, TRUE);
    x += 104 + gap;
    MoveWindow(dataBitsLabel_, x, y + 6, 54, labelHeight, TRUE);
    x += 54;
    MoveWindow(dataBitsCombo_, x, y, 60, 160, TRUE);
    x += 60 + gap;
    MoveWindow(parityLabel_, x, y + 6, 42, labelHeight, TRUE);
    x += 42;
    MoveWindow(parityCombo_, x, y, 92, 180, TRUE);
    x += 92 + gap;
    MoveWindow(stopBitsLabel_, x, y + 6, labelWidth, labelHeight, TRUE);
    x += labelWidth;
    MoveWindow(stopBitsCombo_, x, y, 66, 160, TRUE);
    x += 66 + gap;
    MoveWindow(flowControlLabel_, x, y + 6, 46, labelHeight, TRUE);
    x += 46;
    MoveWindow(flowControlCombo_, x, y, 110, 180, TRUE);
    x += 110 + gap;
    MoveWindow(dtrCheck_, x, y + 4, 52, 24, TRUE);
    x += 52 + gap;
    MoveWindow(rtsCheck_, x, y + 4, 52, 24, TRUE);

    const int statusHeight = 26;
    const int statusY = height - statusHeight - 4;
    const int contentY = connectionY + connectionHeight + 10;
    const int contentHeight = std::max(360, statusY - contentY - 8);
    const int leftWidth = 460;
    const int columnGap = 10;
    const int rightX = margin + leftWidth + columnGap;
    const int rightWidth = std::max(360, width - rightX - margin);

    const int sendY = contentY;
    const int sendHeight = 128;
    MoveWindow(sendGroup_, margin, sendY, leftWidth, sendHeight, TRUE);

    x = margin + groupPad;
    y = sendY + 26;
    const int sendModeWidth = 116;
    const int sendEncodingWidth = 82;
    const int lineEndingWidth = 72;
    const int historyWidth = leftWidth - groupPad * 2 - sendModeWidth - sendEncodingWidth - lineEndingWidth - gap * 3;
    MoveWindow(sendModeCombo_, x, y, sendModeWidth, 180, TRUE);
    x += sendModeWidth + gap;
    MoveWindow(textEncodingCombo_, x, y, sendEncodingWidth, 160, TRUE);
    x += sendEncodingWidth + gap;
    MoveWindow(lineEndingCombo_, x, y, lineEndingWidth, 160, TRUE);
    x += lineEndingWidth + gap;
    MoveWindow(historyCombo_, x, y, historyWidth, 200, TRUE);

    x = margin + groupPad;
    y = sendY + 62;
    const int sendEditWidth = leftWidth - groupPad * 2 - buttonWidth - gap;
    MoveWindow(sendEdit_, x, y, sendEditWidth, row, TRUE);
    MoveWindow(sendButton_, x + sendEditWidth + gap, y, buttonWidth, row, TRUE);

    y = sendY + 96;
    MoveWindow(pauseScrollButton_, margin + groupPad, y, 104, 24, TRUE);
    MoveWindow(clearButton_, margin + groupPad + 104 + gap, y, smallButtonWidth, 24, TRUE);

    const int workflowY = sendY + sendHeight + 10;
    const int workflowHeight = std::max(320, contentHeight - sendHeight - 10);
    MoveWindow(workflowGroup_, margin, workflowY, leftWidth, workflowHeight, TRUE);

    const int innerX = margin + groupPad;
    const int innerWidth = leftWidth - groupPad * 2;
    y = workflowY + 26;
    MoveWindow(workflowHint_, innerX, y, innerWidth, 24, TRUE);

    y += 32;
    MoveWindow(scanSectionLabel_, innerX, y, innerWidth, labelHeight, TRUE);
    y += 24;
    x = innerX;
    MoveWindow(scanSlaveLabel_, x, y + 6, 42, labelHeight, TRUE);
    x += 42;
    MoveWindow(scanSlaveEdit_, x, y, 50, row, TRUE);
    x += 50 + gap;
    MoveWindow(scanFunctionLabel_, x, y + 6, 42, labelHeight, TRUE);
    x += 42;
    MoveWindow(scanFunctionCombo_, x, y, innerWidth - (x - innerX), 180, TRUE);

    y += 36;
    x = innerX;
    MoveWindow(scanStartLabel_, x, y + 6, 70, labelHeight, TRUE);
    x += 70;
    MoveWindow(scanStartEdit_, x, y, 72, row, TRUE);
    x += 72 + gap;
    MoveWindow(scanEndLabel_, x, y + 6, 70, labelHeight, TRUE);
    x += 70;
    MoveWindow(scanEndEdit_, x, y, 72, row, TRUE);

    y += 36;
    MoveWindow(modbusButton_, innerX + innerWidth - 140, y, 140, row, TRUE);
    y += 42;
    MoveWindow(analysisSectionLabel_, innerX, y, innerWidth, labelHeight, TRUE);
    y += 24;
    x = innerX;
    MoveWindow(targetStatic_, x, y + 6, 38, labelHeight, TRUE);
    x += 38;
    MoveWindow(targetLabelEdit_, x, y, 126, row, TRUE);
    x += 126 + gap;
    MoveWindow(targetValueStatic_, x, y + 6, 58, labelHeight, TRUE);
    x += 58;
    MoveWindow(targetValueEdit_, x, y, innerX + innerWidth - x, row, TRUE);

    y += 36;
    x = innerX;
    MoveWindow(targetUnitStatic_, x, y + 6, 38, labelHeight, TRUE);
    x += 38;
    MoveWindow(targetUnitEdit_, x, y, 126, row, TRUE);
    x += 126 + gap;
    MoveWindow(toleranceStatic_, x, y + 6, 42, labelHeight, TRUE);
    x += 42;
    MoveWindow(toleranceEdit_, x, y, innerX + innerWidth - x, row, TRUE);

    y += 36;
    x = innerX;
    MoveWindow(candidateStatic_, x, y + 6, 42, labelHeight, TRUE);
    x += 42;
    MoveWindow(candidateCombo_, x, y, innerX + innerWidth - x, 220, TRUE);

    y += 40;
    x = innerX;
    MoveWindow(analysisButton_, x, y, 112, row, TRUE);
    x += 112 + gap;
    MoveWindow(ruleVerifyButton_, x, y, 100, row, TRUE);
    x += 100 + gap;
    MoveWindow(exportReportButton_, x, y, 100, row, TRUE);

    MoveWindow(logGroup_, rightX, contentY, rightWidth, contentHeight, TRUE);
    const int logInnerX = rightX + groupPad;
    const int logInnerWidth = rightWidth - groupPad * 2;
    const int logActionWidth = 58;
    const int logFormatWidth = 148;
    const int logEncodingWidth = 76;
    x = logInnerX;
    y = contentY + 24;
    MoveWindow(logFormatCombo_, x, y, logFormatWidth, 180, TRUE);
    x += logFormatWidth + gap;
    MoveWindow(logEncodingCombo_, x, y, logEncodingWidth, 160, TRUE);
    x += logEncodingWidth + gap;
    MoveWindow(findLogButton_, x, y, logActionWidth, row, TRUE);
    x += logActionWidth + gap;
    MoveWindow(copyLogButton_, x, y, logActionWidth, row, TRUE);
    x += logActionWidth + gap;
    MoveWindow(exportLogButton_, x, y, logActionWidth, row, TRUE);
    y += 34;
    const int logSearchWidth = std::max(120, (logInnerWidth - gap) / 2);
    const int logFilterWidth = std::max(120, logInnerWidth - logSearchWidth - gap);
    MoveWindow(logFilterEdit_, logInnerX, y, logFilterWidth, row, TRUE);
    MoveWindow(logSearchEdit_, logInnerX + logFilterWidth + gap, y, logSearchWidth, row, TRUE);
    MoveWindow(receiveLog_, logInnerX, contentY + 96, logInnerWidth, contentHeight - 110, TRUE);
    MoveWindow(statusText_, margin, statusY, width - margin * 2, statusHeight, TRUE);
}

void NativeMainWindow::setDefaultFonts() {
    for (HWND child = GetWindow(window_, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        addControlFont(child, uiFont_);
    }
}

void NativeMainWindow::refreshPorts() {
    const std::string currentPort = selectedPortName();
    SendMessageW(portCombo_, CB_RESETCONTENT, 0, 0);
    availablePorts_ = Win32SerialEnumerator::availablePorts();
    for (std::size_t index = 0; index < availablePorts_.size(); ++index) {
        const std::wstring display = portDisplayText(availablePorts_[index]);
        const LRESULT item = SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        if (item >= 0) {
            SendMessageW(portCombo_, CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(index + 1));
        }
    }
    if (!availablePorts_.empty()) {
        LRESULT preserved = -1;
        for (std::size_t index = 0; index < availablePorts_.size(); ++index) {
            if (normalizedComPortName(availablePorts_[index].portName) == normalizedComPortName(currentPort)) {
                preserved = static_cast<LRESULT>(index);
                break;
            }
        }
        SendMessageW(portCombo_, CB_SETCURSEL, preserved >= 0 ? static_cast<WPARAM>(preserved) : 0, 0);
        setStatus(uiString(T::RefreshedPortsPrefix) + std::to_wstring(availablePorts_.size()) + uiString(T::PortsUnitSuffix));
    } else {
        setStatus(tx(T::NoPortsStatus));
    }
}

void NativeMainWindow::refreshSendHistory() {
    SendMessageW(historyCombo_, CB_RESETCONTENT, 0, 0);
    sendHistoryEntries_.clear();
    SendMessageW(historyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tx(T::SendHistory)));
    if (store_.isOpen()) {
        sendHistoryEntries_ = store_.recentSendHistory(30);
        for (std::size_t index = 0; index < sendHistoryEntries_.size(); ++index) {
            const native_storage::SendHistoryEntry& item = sendHistoryEntries_[index];
            const LRESULT comboIndex = SendMessageW(historyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8ToWide(item.content).c_str()));
            if (comboIndex >= 0) {
                SendMessageW(historyCombo_, CB_SETITEMDATA, static_cast<WPARAM>(comboIndex), static_cast<LPARAM>(index + 1));
            }
        }
    }
    SendMessageW(historyCombo_, CB_SETCURSEL, 0, 0);
}

void NativeMainWindow::applySelectedHistory() {
    const LRESULT index = SendMessageW(historyCombo_, CB_GETCURSEL, 0, 0);
    if (index <= 0) {
        return;
    }
    const LRESULT itemData = SendMessageW(historyCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    if (itemData <= 0) {
        setControlText(sendEdit_, controlText(historyCombo_));
        return;
    }
    const std::size_t historyIndex = static_cast<std::size_t>(itemData - 1);
    if (historyIndex >= sendHistoryEntries_.size()) {
        return;
    }

    const native_storage::SendHistoryEntry& history = sendHistoryEntries_[historyIndex];
    setControlText(sendEdit_, utf8ToWide(history.content));
    selectComboData(sendModeCombo_, history.payloadMode);
    selectComboData(lineEndingCombo_, history.lineEnding);
    selectComboData(textEncodingCombo_, history.textEncodingCodePage);
    setStatus(tx(T::SendHistoryRestored));
}

void NativeMainWindow::applyLatestSerialProfile() {
    if (!store_.isOpen()) {
        return;
    }
    const auto profile = store_.latestSerialProfile();
    if (!profile.has_value()) {
        return;
    }

    const std::string normalizedProfilePort = normalizedComPortName(profile->portName);
    const std::wstring portName = utf8ToWide(normalizedProfilePort.empty() ? profile->portName : normalizedProfilePort);
    LRESULT portIndex = -1;
    for (std::size_t index = 0; index < availablePorts_.size(); ++index) {
        if (normalizedComPortName(availablePorts_[index].portName) == normalizedProfilePort) {
            portIndex = static_cast<LRESULT>(index);
            break;
        }
    }
    if (portIndex < 0 && !portName.empty()) {
        portIndex = SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(portName.c_str()));
        if (portIndex >= 0) {
            SendMessageW(portCombo_, CB_SETITEMDATA, static_cast<WPARAM>(portIndex), 0);
        }
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
    updateRtsControlState();
    setStatus(uiString(T::RestoredProfilePrefix) + portName + uiString(T::ChinesePeriod));
}

void NativeMainWindow::saveCurrentSerialProfile() {
    if (!store_.isOpen()) {
        setStatus(tx(T::StorageSaveProfileClosed));
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
    setStatus(uiString(T::SavedProfilePrefix) + utf8ToWide(options.portName) + L"\uFF0C" + std::to_wstring(options.baudRate) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::applyUiPreferences() {
    if (!store_.isOpen()) {
        return;
    }
    const auto preferences = store_.latestUiPreferences();
    if (!preferences.has_value()) {
        return;
    }

    selectComboData(logFormatCombo_, preferences->logFormat);
    selectComboData(logEncodingCombo_, preferences->logEncodingCodePage);
    selectComboData(sendModeCombo_, preferences->sendPayloadMode);
    selectComboData(textEncodingCombo_, preferences->sendTextEncodingCodePage);
    selectComboData(lineEndingCombo_, preferences->sendLineEnding);
    showLogTimestamps_ = preferences->showLogTimestamps;
    applyLogTheme(preferences->logThemeIndex);
    updateLogTimestampMenu();

    if (preferences->windowWidth >= 1080 && preferences->windowHeight >= 760) {
        RECT currentRect = {};
        GetWindowRect(window_, &currentRect);
        MoveWindow(
            window_,
            preferences->windowLeft >= 0 ? preferences->windowLeft : currentRect.left,
            preferences->windowTop >= 0 ? preferences->windowTop : currentRect.top,
            preferences->windowWidth,
            preferences->windowHeight,
            TRUE);
    }
    setStatus(tx(T::UiPreferencesRestoredStatus));
}

void NativeMainWindow::saveUiPreferences() {
    if (!store_.isOpen() || window_ == nullptr) {
        return;
    }

    RECT windowRect = {};
    GetWindowRect(window_, &windowRect);
    native_storage::UiPreferences preferences;
    preferences.name = "default";
    preferences.logThemeIndex = logThemeIndex_;
    preferences.logFormat = static_cast<int>(selectedComboData(logFormatCombo_, 0));
    preferences.logEncodingCodePage = static_cast<int>(selectedLogCodePage());
    preferences.showLogTimestamps = showLogTimestamps_;
    preferences.sendPayloadMode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    preferences.sendTextEncodingCodePage = static_cast<int>(selectedTextCodePage());
    preferences.sendLineEnding = static_cast<int>(selectedComboData(lineEndingCombo_, 0));
    preferences.windowLeft = windowRect.left;
    preferences.windowTop = windowRect.top;
    preferences.windowWidth = windowRect.right - windowRect.left;
    preferences.windowHeight = windowRect.bottom - windowRect.top;
    preferences.updatedAtUtc = timestampText();
    store_.saveUiPreferences(preferences);
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
        setStatus(tx(T::AlreadyConnected));
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
    appendLog(uiString(T::SystemConnectedPrefix) + utf8ToWide(serialPort_.endpoint()));
    SetWindowTextW(connectButton_, tx(T::DisconnectButton));
    updateRtsControlState();
    saveCurrentSerialProfile();
    setStatus(tx(T::ConnectedStatus));
}

void NativeMainWindow::disconnectSerial() {
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::wstring endpoint = utf8ToWide(serialPort_.endpoint());
    serialPort_.close();
    appendLog(uiString(T::SystemDisconnectedPrefix) + endpoint);
    SetWindowTextW(connectButton_, tx(T::ConnectButton));
    updateRtsControlState();
    setStatus(tx(T::DisconnectedStatus));
}

void NativeMainWindow::sendPayload() {
    if (!serialPort_.isOpen()) {
        setStatus(tx(T::SerialNotConnectedSend));
        return;
    }

    std::wstring errorText;
    const std::vector<std::uint8_t> payload = payloadFromInput(&errorText);
    if (!errorText.empty()) {
        setStatus(errorText);
        return;
    }
    if (payload.empty()) {
        setStatus(tx(T::EmptyPayload));
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
        history.textEncodingCodePage = static_cast<int>(selectedTextCodePage());
        history.sentAtUtc = timestampText();
        store_.saveSendHistory(history);
        refreshSendHistory();
    }
    appendPayloadLog(NativeLogKind::Tx, payload);
    setStatus(uiString(T::SentPrefix) + std::to_wstring(result.byteCount) + uiString(T::BytesSuffix));
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
        appendPayloadLog(NativeLogKind::Rx, payload);
    }
}

void NativeMainWindow::handleSerialFailure(const std::string& message) {
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::string endpoint = serialPort_.endpoint();
    serialPort_.close();
    SetWindowTextW(connectButton_, tx(T::ConnectButton));
    updateRtsControlState();
    appendLog(NativeLogKind::Error, uiString(T::SystemSerialFailedPrefix) + utf8ToWide(message));
    const bool autoReconnect = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (autoReconnect && lastOpenOptions_.has_value()) {
        reconnectPortName_ = endpoint;
        waitingReconnect_ = true;
        SetTimer(window_, IDT_RECONNECT, 2000, nullptr);
        setStatus(uiString(T::ReconnectWaitingPrefix) + utf8ToWide(endpoint));
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
        setStatus(uiString(T::WaitingReconnectPrefix) + utf8ToWide(reconnectPortName_) + uiString(T::PortNotReadySuffix));
        return;
    }

    SerialOpenOptions options = *lastOpenOptions_;
    options.portName = reconnectPortName_;
    if (!serialPort_.open(options)) {
        setStatus(uiString(T::AutoReconnectFailedPrefix) + utf8ToWide(serialPort_.lastErrorText()));
        waitingReconnect_ = false;
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    waitingReconnect_ = false;
    KillTimer(window_, IDT_RECONNECT);
    SetWindowTextW(connectButton_, tx(T::DisconnectButton));
    updateRtsControlState();
    appendLog(uiString(T::SystemReconnectOkPrefix) + utf8ToWide(serialPort_.endpoint()));
    setStatus(tx(T::AutoReconnectOk));
}

void NativeMainWindow::appendLog(const std::wstring& line) {
    appendLog(NativeLogKind::System, line);
}

void NativeMainWindow::appendLog(NativeLogKind kind, const std::wstring& line) {
    NativeLogEntry entry;
    entry.kind = kind;
    entry.timestamp = localClockText();
    entry.text = line;
    addLogEntry(std::move(entry));
}

void NativeMainWindow::appendPayloadLog(NativeLogKind kind, const std::vector<std::uint8_t>& payload) {
    const wchar_t* prefix = L"[DATA]";
    switch (kind) {
    case NativeLogKind::Tx:
        prefix = L"[TX]";
        break;
    case NativeLogKind::Rx:
        prefix = L"[RX]";
        break;
    case NativeLogKind::ModbusTx:
        prefix = L"[Modbus TX]";
        break;
    case NativeLogKind::ModbusRx:
        prefix = L"[Modbus RX]";
        break;
    case NativeLogKind::System:
        prefix = L"[\u7CFB\u7EDF]";
        break;
    case NativeLogKind::Error:
        prefix = L"[\u9519\u8BEF]";
        break;
    }
    NativeLogEntry entry;
    entry.kind = kind;
    entry.timestamp = localClockText();
    entry.payloadPrefix = prefix;
    entry.payload = payload;
    entry.hasPayload = true;
    addLogEntry(std::move(entry));
}

void NativeMainWindow::clearLog() {
    logEntries_.clear();
    visibleLogChars_ = 0;
    SetWindowTextW(receiveLog_, L"");
    hiddenLogLineCount_ = 0;
    lastLogSearchOffset_ = 0;
    lastLogSearchText_.clear();
    setStatus(tx(T::ClearLogStatus));
}

void NativeMainWindow::rebuildLogView() {
    if (receiveLog_ == nullptr) {
        return;
    }

    std::vector<std::pair<NativeLogKind, std::wstring>> visibleLines;
    std::size_t visibleChars = 0;
    for (const NativeLogEntry& entry : logEntries_) {
        if (!logEntryMatchesFilter(entry)) {
            continue;
        }
        std::wstring rendered = renderLogEntry(entry);
        const std::size_t renderedSize = rendered.size();
        while (!visibleLines.empty() && visibleChars + renderedSize > kMaxVisibleLogChars) {
            visibleChars -= visibleLines.front().second.size();
            visibleLines.erase(visibleLines.begin());
        }
        if (renderedSize <= kMaxVisibleLogChars) {
            visibleChars += renderedSize;
            visibleLines.emplace_back(entry.kind, std::move(rendered));
        }
    }

    SendMessageW(receiveLog_, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(receiveLog_, L"");
    visibleLogChars_ = 0;
    for (const auto& line : visibleLines) {
        appendVisibleLogText(line.first, line.second);
    }
    SendMessageW(receiveLog_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(receiveLog_, nullptr, TRUE);
    lastLogSearchOffset_ = 0;
}

void NativeMainWindow::appendVisibleLogEntry(const NativeLogEntry& entry) {
    appendVisibleLogText(entry.kind, renderLogEntry(entry));
}

void NativeMainWindow::appendVisibleLogText(NativeLogKind kind, const std::wstring& text) {
    if (receiveLog_ == nullptr) {
        return;
    }
    SendMessageW(receiveLog_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    if (receiveLogUsesRichEdit_) {
        CHARFORMAT2W format = {};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        format.crTextColor = logColorForKind(kind, logThemeIndex_);
        SendMessageW(receiveLog_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    }
    SendMessageW(receiveLog_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    visibleLogChars_ += text.size();
}

void NativeMainWindow::addLogEntry(NativeLogEntry entry) {
    entry.text = sanitizeLogText(entry.text);
    bool trimmed = false;
    logEntries_.push_back(std::move(entry));
    while (logEntries_.size() > kMaxLogEntries) {
        logEntries_.pop_front();
        trimmed = true;
    }

    if (scrollPaused_) {
        ++hiddenLogLineCount_;
        setStatus(uiString(T::ScrollPausedPrefix) + std::to_wstring(hiddenLogLineCount_) + uiString(T::HiddenLinesSuffix));
        return;
    }

    if (trimmed) {
        rebuildLogView();
        return;
    }

    const NativeLogEntry& latest = logEntries_.back();
    if (logEntryMatchesFilter(latest)) {
        appendVisibleLogEntry(latest);
        if (visibleLogChars_ > kMaxVisibleLogChars) {
            rebuildLogView();
        }
    }
}

std::wstring NativeMainWindow::renderLogEntry(const NativeLogEntry& entry) const {
    std::wstring line;
    if (showLogTimestamps_) {
        line += L"[";
        line += entry.timestamp;
        line += L"] ";
    }
    if (entry.hasPayload) {
        line += entry.payloadPrefix;
        line += L" ";
        line += formatPayloadForLog(entry.payload);
    } else {
        line += entry.text;
    }
    line = clipRenderedLogLine(sanitizeLogText(line));
    line += L"\r\n";
    return line;
}

bool NativeMainWindow::logEntryMatchesFilter(const NativeLogEntry& entry) const {
    if (logFilterText_.empty()) {
        return true;
    }
    return containsCaseInsensitive(renderLogEntry(entry), logFilterText_);
}

std::wstring NativeMainWindow::visibleLogText() const {
    return controlText(receiveLog_);
}

void NativeMainWindow::updateLogFilter() {
    logFilterText_ = controlText(logFilterEdit_);
    rebuildLogView();
    std::size_t visibleCount = 0;
    for (const NativeLogEntry& entry : logEntries_) {
        if (logEntryMatchesFilter(entry)) {
            ++visibleCount;
        }
    }
    setStatus(uiString(T::LogFilterChangedPrefix) + std::to_wstring(visibleCount) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::findNextLogMatch() {
    const std::wstring needle = controlText(logSearchEdit_);
    if (needle.empty()) {
        setStatus(tx(T::LogFindEmptyStatus));
        return;
    }

    const std::wstring visibleText = visibleLogText();
    const std::wstring loweredText = lowerCopy(visibleText);
    const std::wstring loweredNeedle = lowerCopy(needle);
    if (needle != lastLogSearchText_) {
        lastLogSearchText_ = needle;
        lastLogSearchOffset_ = 0;
    }

    std::size_t position = loweredText.find(loweredNeedle, lastLogSearchOffset_);
    if (position == std::wstring::npos && lastLogSearchOffset_ > 0) {
        position = loweredText.find(loweredNeedle);
    }
    if (position == std::wstring::npos) {
        setStatus(uiString(T::LogFindNotFoundPrefix) + needle + uiString(T::ChinesePeriod));
        return;
    }

    SendMessageW(receiveLog_, EM_SETSEL, static_cast<WPARAM>(position), static_cast<LPARAM>(position + needle.size()));
    SendMessageW(receiveLog_, EM_SCROLLCARET, 0, 0);
    lastLogSearchOffset_ = position + needle.size();
    setStatus(uiString(T::LogFindMatchedPrefix) + needle + uiString(T::ChinesePeriod));
}

void NativeMainWindow::copyVisibleLogToClipboard() {
    const std::wstring text = visibleLogText();
    if (!OpenClipboard(window_)) {
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) {
        CloseClipboard();
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    std::memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        setStatus(tx(T::LogCopyFailedStatus));
        return;
    }
    CloseClipboard();
    setStatus(tx(T::LogCopiedStatus));
}

void NativeMainWindow::exportVisibleLog() {
    const std::wstring text = visibleLogText();
    if (text.empty()) {
        setStatus(tx(T::LogExportEmptyStatus));
        return;
    }

    wchar_t fileName[MAX_PATH] = {};
    const std::wstring defaultName = uiString(T::LogExportDefaultPrefix)
        + utf8ToWide(timestampIdText())
        + L".txt";
    wcsncpy_s(fileName, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = tx(T::LogExportFilter);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrTitle = tx(T::LogExportDialogTitle);
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }

    const std::string utf8Text = wideToUtf8(text);
    std::ofstream output(std::filesystem::path(fileName), std::ios::binary | std::ios::trunc);
    if (!output) {
        setStatus(uiString(T::LogExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    constexpr unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    output.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    output.write(utf8Text.data(), static_cast<std::streamsize>(utf8Text.size()));
    if (!output) {
        setStatus(uiString(T::LogExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    setStatus(uiString(T::LogExportOkPrefix) + std::wstring(fileName) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::setStatus(const std::wstring& text) {
    SetWindowTextW(statusText_, text.c_str());
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

void NativeMainWindow::setControlText(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

std::vector<std::uint8_t> NativeMainWindow::payloadFromInput(std::wstring* errorText) const {
    const std::wstring text = controlText(sendEdit_);
    const int mode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    std::vector<std::uint8_t> payload;
    if (mode == 1) {
        payload = parseHexPayload(text, errorText);
    } else if (mode == 2) {
        payload = parseDecimalPayload(text, errorText);
    } else if (mode == 3) {
        payload = parseBinaryPayload(text, errorText);
    } else {
        payload = encodeTextPayload(text, selectedTextCodePage(), errorText);
    }
    if (errorText == nullptr || errorText->empty()) {
        appendLineEnding(payload, static_cast<int>(selectedComboData(lineEndingCombo_, 0)));
    }
    return payload;
}

std::wstring NativeMainWindow::formatPayloadForLog(const std::vector<std::uint8_t>& payload) const {
    const int mode = static_cast<int>(selectedComboData(logFormatCombo_, 0));
    switch (mode) {
    case 1:
        return bytesToDecimal(payload);
    case 2:
        return bytesToBinary(payload);
    case 3:
        return decodeBytesToText(payload, selectedLogCodePage());
    case 4:
        return bytesToHex(payload) + L" | " + decodeBytesToText(payload, selectedLogCodePage());
    default:
        return bytesToHex(payload);
    }
}

unsigned int NativeMainWindow::selectedTextCodePage() const {
    return static_cast<unsigned int>(selectedComboData(textEncodingCombo_, CP_UTF8));
}

unsigned int NativeMainWindow::selectedLogCodePage() const {
    return static_cast<unsigned int>(selectedComboData(logEncodingCombo_, CP_UTF8));
}

std::string NativeMainWindow::selectedPortName() const {
    const LRESULT index = SendMessageW(portCombo_, CB_GETCURSEL, 0, 0);
    if (index >= 0) {
        const LRESULT itemData = SendMessageW(portCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
        if (itemData > 0) {
            const std::size_t portIndex = static_cast<std::size_t>(itemData - 1);
            if (portIndex < availablePorts_.size()) {
                return availablePorts_[portIndex].portName;
            }
        }
    }

    std::wstring text = controlText(portCombo_);
    const std::wstring separator = L" - ";
    const std::size_t separatorPosition = text.find(separator);
    if (separatorPosition != std::wstring::npos) {
        text.resize(separatorPosition);
    }
    return wideToUtf8(text);
}

SerialOpenOptions NativeMainWindow::currentOpenOptions() const {
    SerialOpenOptions options;
    options.portName = selectedPortName();
    options.baudRate = std::max(1, textToInt(baudCombo_, 115200));
    options.dataBits = static_cast<int>(selectedComboData(dataBitsCombo_, 8));
    options.parity = static_cast<SerialParity>(selectedComboData(parityCombo_, static_cast<LPARAM>(SerialParity::None)));
    options.stopBits = static_cast<SerialStopBits>(selectedComboData(stopBitsCombo_, static_cast<LPARAM>(SerialStopBits::One)));
    options.flowControl = static_cast<SerialFlowControl>(selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None)));
    options.dataTerminalReady = SendMessageW(dtrCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.requestToSend = SendMessageW(rtsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return options;
}

void NativeMainWindow::applySerialLineControl(WORD controlId) {
    const bool enabled = SendMessageW(controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!serialPort_.isOpen()) {
        setStatus(std::wstring(controlId == IDC_DTR_CHECK ? tx(T::DtrAppliedPrefix) : tx(T::RtsAppliedPrefix))
            + (enabled ? tx(T::SignalEnabledSuffix) : tx(T::SignalDisabledSuffix))
            + L" \u4E0B\u6B21\u8FDE\u63A5\u751F\u6548\u3002");
        return;
    }

    bool ok = false;
    if (controlId == IDC_DTR_CHECK) {
        ok = serialPort_.setDataTerminalReady(enabled);
        if (ok && lastOpenOptions_.has_value()) {
            lastOpenOptions_->dataTerminalReady = enabled;
        }
    } else {
        ok = serialPort_.setRequestToSend(enabled);
        if (ok && lastOpenOptions_.has_value()) {
            lastOpenOptions_->requestToSend = enabled;
        }
    }

    if (!ok) {
        HWND control = controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_;
        SendMessageW(control, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
        setStatus(utf8ToWide(serialPort_.lastErrorText()));
        updateRtsControlState();
        return;
    }

    setStatus(std::wstring(controlId == IDC_DTR_CHECK ? tx(T::DtrAppliedPrefix) : tx(T::RtsAppliedPrefix))
        + (enabled ? tx(T::SignalEnabledSuffix) : tx(T::SignalDisabledSuffix)));
}

void NativeMainWindow::updateRtsControlState() {
    const bool selectedHardwareRtsCts = selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None))
        == static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts);
    const bool hardwareManaged = serialPort_.isOpen() ? serialPort_.usesHardwareRtsCts() : selectedHardwareRtsCts;
    EnableWindow(rtsCheck_, hardwareManaged ? FALSE : TRUE);
}

void NativeMainWindow::applyLogTheme(int themeIndex) {
    logThemeIndex_ = std::clamp(themeIndex, 0, 2);
    if (menu_ != nullptr) {
        HMENU viewMenu = GetSubMenu(menu_, 4);
        CheckMenuRadioItem(
            viewMenu != nullptr ? viewMenu : menu_,
            IDM_VIEW_THEME_DEFAULT,
            IDM_VIEW_THEME_HIGH_CONTRAST,
            logThemeIndex_ == 0 ? IDM_VIEW_THEME_DEFAULT : (logThemeIndex_ == 1 ? IDM_VIEW_THEME_SOFT : IDM_VIEW_THEME_HIGH_CONTRAST),
            MF_BYCOMMAND);
    }

    if (receiveLog_ == nullptr || !receiveLogUsesRichEdit_) {
        return;
    }

    const NativeLogPalette palette = logPalette(logThemeIndex_);
    SendMessageW(receiveLog_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(palette.background));
    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = palette.normal;
    SendMessageW(receiveLog_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
    InvalidateRect(receiveLog_, nullptr, TRUE);
}

void NativeMainWindow::updateLogTimestampMenu() {
    if (menu_ == nullptr) {
        return;
    }
    HMENU viewMenu = GetSubMenu(menu_, 4);
    CheckMenuItem(
        viewMenu != nullptr ? viewMenu : menu_,
        IDM_VIEW_SHOW_TIMESTAMPS,
        MF_BYCOMMAND | (showLogTimestamps_ ? MF_CHECKED : MF_UNCHECKED));
}

void NativeMainWindow::runModbusScan() {
    if (!serialPort_.isOpen()) {
        setStatus(tx(T::ConnectBeforeModbus));
        return;
    }
    if (!store_.isOpen()) {
        setStatus(tx(T::StorageModbusClosed));
        return;
    }

    modbus_core::ScanPlanOptions options;
    options.slaveId = textToInt(scanSlaveEdit_, 1);
    options.functionCode = static_cast<::svm::core::Byte>(selectedComboData(scanFunctionCombo_, 3));
    options.range.startAddress = textToInt(scanStartEdit_, 0);
    options.range.endAddress = textToInt(scanEndEdit_, 15);
    options.blockSize = 16;
    options.requestIntervalMs = 30;
    options.retryCount = 0;
    options.safetyLevel = modbus_core::ScanSafetyLevel::Custom;

    const auto planResult = modbus_core::buildScanPlan(options);
    if (!planResult.ok) {
        setStatus(utf8ToWide(planResult.errorMessage));
        MessageBoxW(window_, utf8ToWide(planResult.errorMessage).c_str(), tx(T::ModbusInvalidTitle), MB_ICONWARNING | MB_OK);
        return;
    }

    const std::string scanSessionId = "scan-" + timestampIdText();
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

    appendLog(uiString(T::SystemModbusStartPrefix) + utf8ToWide(scanSessionId));
    setStatus(tx(T::ModbusRunning));

    for (const modbus_core::ScanBlock& block : planResult.plan.blocks) {
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
        appendPayloadLog(NativeLogKind::ModbusTx, block.requestFrame);
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
            appendPayloadLog(NativeLogKind::ModbusRx, response);
        }

        if (response.empty()) {
            attempt.status = "timeout";
            if (attempt.errorMessage.empty()) {
                attempt.errorMessage = wideToUtf8(tx(T::ModbusTimeout));
            }
            ++execution.session.failedBlockCount;
            execution.attempts.push_back(std::move(attempt));
            continue;
        }

        const auto parsed = modbus_core::parseReadResponse(
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
        for (const modbus_core::RegisterObservation& observation : parsed.observations) {
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

    const std::wstring summary = uiString(T::ModbusSummaryPrefix)
        + std::to_wstring(execution.session.successBlockCount)
        + uiString(T::ModbusFailedBlocks)
        + std::to_wstring(execution.session.failedBlockCount)
        + uiString(T::ModbusObservations)
        + std::to_wstring(execution.observations.size())
        + uiString(T::ChinesePeriod);
    appendLog(std::wstring(L"[") + L"\u7CFB\u7EDF] " + summary);
    setStatus(summary);
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
    const double targetValue = textToDouble(targetValueEdit_, 0.0, &targetOk);
    if (!targetOk) {
        setStatus(tx(T::AnalysisInvalidTarget));
        return;
    }
    bool toleranceOk = false;
    const double tolerance = std::max(0.0, textToDouble(toleranceEdit_, 0.0, &toleranceOk));

    std::vector<analysis_core::RegisterSample> samples;
    samples.reserve(observations.size());
    for (const native_storage::ScanObservationRecord& observation : observations) {
        samples.push_back(sampleFromObservation(observation));
    }

    analysis_core::TargetValue target;
    target.label = wideToUtf8(controlText(targetLabelEdit_));
    target.value = targetValue;
    target.unit = wideToUtf8(controlText(targetUnitEdit_));

    analysis_core::CandidateGenerationOptions options;
    options.scaleTransforms = {{1.0, 0.0}, {0.1, 0.0}, {0.01, 0.0}, {0.001, 0.0}, {10.0, 0.0}};
    options.tolerance.absolute = toleranceOk ? tolerance : 0.0;
    options.maxCandidates = 30;

    const auto result = analysis_core::generateValueCandidates(samples, target, options);
    if (!result.success) {
        setStatus(utf8ToWide(result.errorMessage));
        return;
    }
    if (result.candidates.empty()) {
        setStatus(tx(T::AnalysisNoCandidates));
        return;
    }

    const std::string runId = "match-" + timestampIdText();
    native_storage::MatchRunRecord run;
    run.runId = runId;
    run.sourceScanSessionId = session->sessionId;
    run.targetLabel = target.label;
    run.targetValue = target.value;
    run.targetUnit = target.unit;
    run.sampledAtUtc = timestampText();
    run.toleranceAbsolute = options.tolerance.absolute;
    run.toleranceRelativeRatio = options.tolerance.relativeRatio;
    run.createdAtUtc = timestampText();

    std::vector<native_storage::MatchCandidateRecord> candidateRecords;
    candidateRecords.reserve(result.candidates.size());
    for (std::size_t index = 0; index < result.candidates.size(); ++index) {
        candidateRecords.push_back(candidateRecordFromCore(
            result.candidates[index],
            runId,
            static_cast<int>(index),
            timestampText()));
    }

    if (!store_.saveMatchRun(run, candidateRecords)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return;
    }
    latestMatchRunId_ = runId;
    refreshCandidateCombo(runId);

    const std::wstring summary = uiString(T::AnalysisSavedPrefix)
        + utf8ToWide(session->sessionId)
        + uiString(T::AnalysisSavedMid)
        + std::to_wstring(result.candidates.size())
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
    if (!latestVerificationRunId_.empty()) {
        run = store_.ruleVerificationRun(latestVerificationRunId_);
    }
    if (!run.has_value()) {
        run = store_.latestRuleVerificationRun();
    }
    if (!run.has_value()) {
        setStatus(tx(T::ExportNoRun));
        return;
    }

    const auto resultRecords = store_.ruleVerificationResults(run->verificationRunId);
    report_core::RuleVerificationRun reportRun;
    reportRun.verificationRunId = run->verificationRunId;
    reportRun.sourceScanSessionId = run->sourceScanSessionId;
    reportRun.ruleCount = run->ruleCount;
    reportRun.verifiedCount = run->verifiedCount;
    reportRun.missingCount = run->missingCount;
    reportRun.unsupportedCount = run->unsupportedCount;
    reportRun.createdAtText = run->createdAtUtc;

    std::vector<report_core::RuleVerificationResult> reportResults;
    reportResults.reserve(resultRecords.size());
    for (const native_storage::RuleVerificationResultRecord& record : resultRecords) {
        report_core::RuleVerificationResult result;
        result.fieldName = record.fieldName;
        result.unit = record.unit;
        result.verified = record.verified;
        result.statusText = record.statusText;
        result.slaveId = record.slaveId;
        result.functionCode = record.functionCode;
        result.startAddress = record.startAddress;
        result.registerCount = record.registerCount;
        result.observationIds = record.observationIds;
        result.rawRegisters = record.rawRegisters;
        result.engineeringValue = record.engineeringValue;
        result.interpretationText = record.interpretationText;
        result.evidenceText = record.evidenceText;
        reportResults.push_back(std::move(result));
    }

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

    const std::string markdown = report_core::renderRuleVerificationMarkdownReport(reportRun, reportResults);
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
    if (!store_.isOpen() || runId.empty()) {
        SendMessageW(candidateCombo_, CB_SETCURSEL, 0, 0);
        return;
    }

    const auto candidates = store_.matchCandidates(runId);
    for (const native_storage::MatchCandidateRecord& candidate : candidates) {
        const std::wstring display = candidateDisplayText(candidate);
        const LRESULT index = SendMessageW(candidateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        if (index >= 0) {
            SendMessageW(candidateCombo_, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(candidate.id));
        }
    }
    SendMessageW(candidateCombo_, CB_SETCURSEL, candidates.empty() ? 0 : 1, 0);
}

std::optional<native_storage::MatchCandidateRecord> NativeMainWindow::selectedCandidate() const {
    if (!store_.isOpen()) {
        return std::nullopt;
    }
    std::string runId = latestMatchRunId_;
    if (runId.empty()) {
        const auto run = store_.latestMatchRun();
        if (!run.has_value()) {
            return std::nullopt;
        }
        runId = run->runId;
    }

    const LRESULT index = SendMessageW(candidateCombo_, CB_GETCURSEL, 0, 0);
    const LRESULT itemData = index >= 0 ? SendMessageW(candidateCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) : 0;
    const std::int64_t selectedId = itemData == CB_ERR ? 0 : static_cast<std::int64_t>(itemData);
    const auto candidates = store_.matchCandidates(runId);
    if (selectedId > 0) {
        for (const native_storage::MatchCandidateRecord& candidate : candidates) {
            if (candidate.id == selectedId) {
                return candidate;
            }
        }
    }
    if (!candidates.empty()) {
        return candidates.front();
    }
    return std::nullopt;
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
    rule.fieldName = wideToUtf8(ruleDisplayName(candidate, controlText(targetLabelEdit_)));
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
    rule.unit = wideToUtf8(controlText(targetUnitEdit_));
    rule.confidenceLevel = wideToUtf8(candidate.score >= 85.0 ? L"\u9AD8" : (candidate.score >= 65.0 ? L"\u4E2D" : L"\u4F4E"));
    rule.stabilityScore = candidate.score;
    rule.evidenceSummary = candidate.evidenceText;
    rule.createdAtUtc = timestampText();
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

    auto findObservation = [&observations](const native_storage::ProtocolFieldRuleRecord& rule, int address) -> std::optional<native_storage::ScanObservationRecord> {
        for (auto iterator = observations.rbegin(); iterator != observations.rend(); ++iterator) {
            if (iterator->slaveId == rule.slaveId
                && iterator->functionCode == rule.functionCode
                && iterator->address == address) {
                return *iterator;
            }
        }
        return std::nullopt;
    };

    native_storage::RuleVerificationRunRecord run;
    run.verificationRunId = "verify-" + timestampIdText();
    run.sourceScanSessionId = session.sessionId;
    run.ruleCount = static_cast<int>(rules.size());
    run.createdAtUtc = timestampText();

    std::vector<native_storage::RuleVerificationResultRecord> results;
    results.reserve(rules.size());
    for (const native_storage::ProtocolFieldRuleRecord& rule : rules) {
        native_storage::RuleVerificationResultRecord result;
        result.verificationRunId = run.verificationRunId;
        result.ruleId = rule.ruleId;
        result.fieldName = rule.fieldName;
        result.unit = rule.unit;
        result.candidateType = rule.candidateType;
        result.sourceScanSessionId = session.sessionId;
        result.slaveId = rule.slaveId;
        result.functionCode = rule.functionCode;
        result.startAddress = rule.startAddress;
        result.registerCount = rule.registerCount;

        std::vector<std::uint16_t> registers;
        registers.reserve(static_cast<std::size_t>(std::max(0, rule.registerCount)));
        bool missing = false;
        int missingAddress = rule.startAddress;
        for (int offset = 0; offset < rule.registerCount; ++offset) {
            const int address = rule.startAddress + offset;
            const auto observation = findObservation(rule, address);
            if (!observation.has_value()) {
                missing = true;
                missingAddress = address;
                break;
            }
            result.observationIds.push_back(observation->id);
            result.rawRegisters.push_back(observation->value);
            result.observedAtUtc = observation->observedAtUtc;
            registers.push_back(static_cast<std::uint16_t>(observation->value));
        }

        if (missing || registers.empty()) {
            result.verified = false;
            result.statusText = wideToUtf8(std::wstring(L"\u7F3A\u5C11\u5730\u5740 ")
                + std::to_wstring(missingAddress)
                + L" \u7684\u89C2\u6D4B\uFF0C\u65E0\u6CD5\u9A8C\u8BC1\u3002");
            result.evidenceText = result.statusText;
            ++run.missingCount;
            results.push_back(std::move(result));
            continue;
        }

        const auto decoded = analysis_core::decodeNumericValue(rule.candidateType, rule.wordOrder, rule.byteOrder, registers);
        if (!decoded.has_value()) {
            result.verified = false;
            result.statusText = wideToUtf8(L"\u89E3\u7801\u5931\u8D25\uFF1A\u89C4\u5219\u7C7B\u578B\u4E0E\u5BC4\u5B58\u5668\u6570\u91CF\u4E0D\u5339\u914D\u3002");
            result.evidenceText = result.statusText;
            ++run.unsupportedCount;
            results.push_back(std::move(result));
            continue;
        }

        result.verified = true;
        result.statusText = wideToUtf8(L"\u5DF2\u9A8C\u8BC1");
        result.decodedValue = *decoded;
        result.engineeringValue = *decoded * rule.scaleMultiplier + rule.scaleOffset;
        if (rule.candidateType == "BitFlags" && !registers.empty()) {
            result.interpretationText = analysis_core::bitFlagInterpretationText(rule.interpretationMap, registers.front());
        } else if (rule.candidateType == "EnumMap") {
            result.interpretationText = analysis_core::enumMapInterpretationText(rule.interpretationMap, static_cast<int>(*decoded));
        }
        result.evidenceText = wideToUtf8(std::wstring(L"\u5B57\u6BB5\u9A8C\u8BC1\u6210\u529F\uFF1A")
            + utf8ToWide(rule.fieldName)
            + L"\uFF0C\u6765\u81EA\u626B\u63CF "
            + utf8ToWide(session.sessionId)
            + L"\u3002");
        ++run.verifiedCount;
        results.push_back(std::move(result));
    }

    if (!store_.saveRuleVerificationRun(run, results)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return false;
    }
    latestVerificationRunId_ = run.verificationRunId;

    const std::wstring summary = uiString(T::RuleVerifySavedPrefix)
        + utf8ToWide(run.verificationRunId)
        + uiString(T::RulesTotalPrefix)
        + std::to_wstring(run.ruleCount)
        + uiString(T::RulesVerifiedPrefix)
        + std::to_wstring(run.verifiedCount)
        + uiString(T::RulesMissingPrefix)
        + std::to_wstring(run.missingCount)
        + uiString(T::RulesUnsupportedPrefix)
        + std::to_wstring(run.unsupportedCount)
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
    event.timestampUtc = timestampText();
    event.endpoint = serialPort_.endpoint();
    event.payload = payload;
    store_.appendRawEvent(event);
}

} // namespace svm::win32

#endif
