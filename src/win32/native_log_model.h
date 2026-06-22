#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace svm::win32 {

enum class NativeLogKind {
    System,
    Tx,
    Rx,
    ModbusTx,
    ModbusRx,
    Error,
};

struct NativeLogEntry {
    NativeLogKind kind = NativeLogKind::System;
    std::wstring timestamp;
    std::wstring text;
    std::wstring payloadPrefix;
    std::vector<std::uint8_t> payload;
    bool hasPayload = false;
};

struct NativePendingLogLine {
    NativeLogKind kind = NativeLogKind::System;
    std::wstring text;
};

std::wstring sanitizeLogText(std::wstring_view text);
std::wstring lowerCopy(std::wstring_view text);
bool containsCaseInsensitive(std::wstring_view haystack, std::wstring_view needle);
std::wstring clipRenderedLogLine(std::wstring text, std::size_t maxChars, std::wstring_view suffix);
std::wstring_view nativeLogPayloadPrefix(NativeLogKind kind);

} // namespace svm::win32
