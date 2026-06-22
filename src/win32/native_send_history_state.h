#pragma once

#include "native_storage/native_session_store.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace svm::win32 {

using NativeSendHistoryItemData = std::intptr_t;

class NativeSendHistoryState final {
public:
    void clear();
    void setEntries(std::vector<native_storage::SendHistoryEntry> entries);

    const std::vector<native_storage::SendHistoryEntry>& entries() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;

    NativeSendHistoryItemData itemDataForIndex(std::size_t index) const noexcept;
    std::optional<std::size_t> indexFromItemData(NativeSendHistoryItemData itemData) const noexcept;
    std::optional<native_storage::SendHistoryEntry> entryFromItemData(NativeSendHistoryItemData itemData) const;

private:
    std::vector<native_storage::SendHistoryEntry> entries_;
};

native_storage::SendHistoryEntry nativeMakeSendHistoryEntry(
    std::string content,
    int payloadMode,
    int lineEnding,
    int textEncodingCodePage,
    std::string sentAtUtc);

} // namespace svm::win32
