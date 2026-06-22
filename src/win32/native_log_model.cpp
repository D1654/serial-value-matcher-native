#include "win32/native_log_model.h"

#include <cwctype>

namespace svm::win32 {

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

std::wstring clipRenderedLogLine(std::wstring text, std::size_t maxChars, std::wstring_view suffix) {
    if (text.size() <= maxChars) {
        return text;
    }
    const std::size_t keep = maxChars > suffix.size()
        ? maxChars - suffix.size()
        : maxChars;
    text.resize(keep);
    text.append(suffix);
    return text;
}

std::wstring_view nativeLogPayloadPrefix(NativeLogKind kind) {
    switch (kind) {
    case NativeLogKind::Tx:
        return L"[TX]";
    case NativeLogKind::Rx:
        return L"[RX]";
    case NativeLogKind::ModbusTx:
        return L"[Modbus TX]";
    case NativeLogKind::ModbusRx:
        return L"[Modbus RX]";
    case NativeLogKind::System:
        return L"[\u7CFB\u7EDF]";
    case NativeLogKind::Error:
        return L"[\u9519\u8BEF]";
    }
    return L"[DATA]";
}

} // namespace svm::win32
