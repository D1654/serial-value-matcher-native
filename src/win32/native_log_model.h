#pragma once

#include "transport/serial_types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace svm::win32 {

inline constexpr std::size_t kNativeSerialLogEndpointMaxBytes = 256;
inline constexpr std::size_t kNativeSerialLogTextMaxChars = 1024;

enum class NativeLogKind {
    System,
    Tx,
    Rx,
    ModbusTx,
    ModbusRx,
    Error,
};

struct NativeSerialLogMetadata {
    svm::transport::SerialDataDirection direction = svm::transport::SerialDataDirection::None;
    svm::transport::SerialOperationKind operation = svm::transport::SerialOperationKind::Read;
    svm::transport::SerialOperationStatus status = svm::transport::SerialOperationStatus::Failed;
    svm::transport::SerialDeadlineStatus deadlineStatus = svm::transport::SerialDeadlineStatus::NotSet;
    svm::transport::SerialErrorCategory errorCategory = svm::transport::SerialErrorCategory::None;
    svm::transport::SerialOperationId requestId = svm::transport::kUnassignedSerialOperationId;
    svm::transport::SerialSessionGeneration generation = svm::transport::kUnassignedSerialSessionGeneration;
    std::size_t byteCount = 0;
    std::string endpoint;
    std::uint32_t nativeCode = 0;
    std::uint32_t commErrorMask = 0;
    std::optional<std::size_t> inputQueueBytes;
    std::optional<std::size_t> outputQueueBytes;
};

struct NativeLogEntry {
    NativeLogKind kind = NativeLogKind::System;
    std::wstring timestamp;
    std::wstring text;
    std::wstring payloadPrefix;
    std::vector<std::uint8_t> payload;
    std::optional<NativeSerialLogMetadata> serialMetadata;
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
NativeLogEntry nativeMakeTextLogEntry(NativeLogKind kind, std::wstring timestamp, std::wstring text);
NativeLogEntry nativeMakePayloadLogEntry(NativeLogKind kind, std::wstring timestamp, std::vector<std::uint8_t> payload);
NativeSerialLogMetadata nativeSerialLogMetadata(const svm::transport::SerialOperationResult& result);
NativeLogEntry nativeMakeSerialTextLogEntry(
    NativeLogKind kind,
    std::wstring timestamp,
    std::wstring text,
    const svm::transport::SerialOperationResult& result);
NativeLogEntry nativeMakeSerialPayloadLogEntry(
    NativeLogKind kind,
    std::wstring timestamp,
    std::vector<std::uint8_t> payload,
    const svm::transport::SerialOperationResult& result);

} // namespace svm::win32
