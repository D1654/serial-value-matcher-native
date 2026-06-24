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

struct NativeLogFilterUpdate {
    bool changed = false;
    std::wstring filterText;
};

struct NativeLogSearchResult {
    bool found = false;
    std::size_t position = 0;
    std::size_t length = 0;
    bool wrapped = false;
};

class NativeLogFilterState final {
public:
    const std::wstring& filterText() const noexcept;
    const std::wstring& loweredFilterText() const noexcept;
    std::size_t searchOffset() const noexcept;
    const std::wstring& searchText() const noexcept;

    NativeLogFilterUpdate setFilterText(std::wstring text);
    void clear();
    void resetSearch();
    NativeLogSearchResult findNext(std::wstring_view visibleText, std::wstring_view needle);

private:
    std::wstring filterText_;
    std::wstring loweredFilterText_;
    std::wstring searchText_;
    std::size_t searchOffset_ = 0;
};

std::wstring sanitizeLogText(std::wstring_view text);
std::wstring lowerCopy(std::wstring_view text);
bool containsLoweredNeedle(std::wstring_view haystack, std::wstring_view loweredNeedle);
bool containsCaseInsensitive(std::wstring_view haystack, std::wstring_view needle);
std::wstring clipRenderedLogLine(std::wstring text, std::size_t maxChars, std::wstring_view suffix);
std::wstring_view nativeLogPayloadPrefix(NativeLogKind kind);

} // namespace svm::win32
