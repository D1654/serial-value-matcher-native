#include "win32/native_log_model.h"

#include <cwctype>
#include <utility>

namespace svm::win32 {
namespace {

template <typename String>
void clipField(String& value, std::size_t limit) {
    if (value.size() > limit) {
        value.resize(limit);
    }
}

} // namespace

const std::wstring& NativeLogFilterState::filterText() const noexcept {
    return filterText_;
}

const std::wstring& NativeLogFilterState::loweredFilterText() const noexcept {
    return loweredFilterText_;
}

std::size_t NativeLogFilterState::searchOffset() const noexcept {
    return searchOffset_;
}

const std::wstring& NativeLogFilterState::searchText() const noexcept {
    return searchText_;
}

NativeLogFilterUpdate NativeLogFilterState::setFilterText(std::wstring text) {
    if (text == filterText_) {
        return {false, filterText_};
    }
    filterText_ = std::move(text);
    loweredFilterText_ = lowerCopy(filterText_);
    resetSearch();
    return {true, filterText_};
}

void NativeLogFilterState::clear() {
    filterText_.clear();
    loweredFilterText_.clear();
    resetSearch();
}

void NativeLogFilterState::resetSearch() {
    searchText_.clear();
    searchOffset_ = 0;
}

NativeLogSearchResult NativeLogFilterState::findNext(std::wstring_view visibleText, std::wstring_view needle) {
    NativeLogSearchResult result;
    if (needle.empty()) {
        resetSearch();
        return result;
    }

    if (needle != searchText_) {
        searchText_ = std::wstring(needle);
        searchOffset_ = 0;
    }

    const std::wstring loweredText = lowerCopy(visibleText);
    const std::wstring loweredNeedle = lowerCopy(needle);
    std::size_t position = loweredText.find(loweredNeedle, searchOffset_);
    if (position == std::wstring::npos && searchOffset_ > 0) {
        position = loweredText.find(loweredNeedle);
        result.wrapped = position != std::wstring::npos;
    }
    if (position == std::wstring::npos) {
        return result;
    }

    result.found = true;
    result.position = position;
    result.length = needle.size();
    searchOffset_ = position + needle.size();
    return result;
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

bool containsLoweredNeedle(std::wstring_view haystack, std::wstring_view loweredNeedle) {
    if (loweredNeedle.empty()) {
        return true;
    }
    return lowerCopy(haystack).find(loweredNeedle) != std::wstring::npos;
}

bool containsCaseInsensitive(std::wstring_view haystack, std::wstring_view needle) {
    return containsLoweredNeedle(haystack, lowerCopy(needle));
}

std::wstring clipRenderedLogLine(std::wstring text, std::size_t maxChars, std::wstring_view suffix) {
    if (text.size() <= maxChars) {
        return text;
    }
    if (maxChars == 0) {
        return {};
    }
    if (suffix.size() >= maxChars) {
        return std::wstring(suffix.substr(0, maxChars));
    }
    const std::size_t keep = maxChars - suffix.size();
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

NativeLogEntry nativeMakeTextLogEntry(NativeLogKind kind, std::wstring timestamp, std::wstring text) {
    NativeLogEntry entry;
    entry.kind = kind;
    entry.timestamp = std::move(timestamp);
    entry.text = std::move(text);
    return entry;
}

NativeLogEntry nativeMakePayloadLogEntry(NativeLogKind kind, std::wstring timestamp, std::vector<std::uint8_t> payload) {
    NativeLogEntry entry;
    entry.kind = kind;
    entry.timestamp = std::move(timestamp);
    entry.payloadPrefix = std::wstring(nativeLogPayloadPrefix(kind));
    entry.payload = std::move(payload);
    entry.hasPayload = true;
    return entry;
}

NativeSerialLogMetadata nativeSerialLogMetadata(
    const svm::transport::SerialOperationResult& result) {
    NativeSerialLogMetadata metadata;
    metadata.direction = svm::transport::serialOperationDirection(result.operation.kind);
    metadata.operation = result.operation.kind;
    metadata.status = result.status;
    metadata.deadlineStatus = result.deadlineStatus;
    metadata.errorCategory = result.error.category;
    metadata.requestId = result.operation.requestId;
    metadata.generation = result.operation.generation;
    metadata.byteCount = result.byteCount;
    metadata.endpoint = result.endpoint;
    clipField(metadata.endpoint, kNativeSerialLogEndpointMaxBytes);
    metadata.nativeCode = result.error.nativeCode;
    metadata.commErrorMask = result.error.commErrorMask;
    metadata.inputQueueBytes = result.error.inputQueueBytes;
    metadata.outputQueueBytes = result.error.outputQueueBytes;
    return metadata;
}

NativeLogEntry nativeMakeSerialTextLogEntry(
    NativeLogKind kind,
    std::wstring timestamp,
    std::wstring text,
    const svm::transport::SerialOperationResult& result) {
    clipField(text, kNativeSerialLogTextMaxChars);
    NativeLogEntry entry = nativeMakeTextLogEntry(kind, std::move(timestamp), std::move(text));
    entry.serialMetadata = nativeSerialLogMetadata(result);
    return entry;
}

NativeLogEntry nativeMakeSerialPayloadLogEntry(
    NativeLogKind kind,
    std::wstring timestamp,
    std::vector<std::uint8_t> payload,
    const svm::transport::SerialOperationResult& result) {
    NativeLogEntry entry = nativeMakePayloadLogEntry(kind, std::move(timestamp), std::move(payload));
    entry.serialMetadata = nativeSerialLogMetadata(result);
    return entry;
}

} // namespace svm::win32
