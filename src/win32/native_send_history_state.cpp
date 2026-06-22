#include "win32/native_send_history_state.h"

#include <utility>

namespace svm::win32 {

void NativeSendHistoryState::clear() {
    entries_.clear();
}

void NativeSendHistoryState::setEntries(std::vector<native_storage::SendHistoryEntry> entries) {
    entries_ = std::move(entries);
}

const std::vector<native_storage::SendHistoryEntry>& NativeSendHistoryState::entries() const noexcept {
    return entries_;
}

std::size_t NativeSendHistoryState::size() const noexcept {
    return entries_.size();
}

bool NativeSendHistoryState::empty() const noexcept {
    return entries_.empty();
}

NativeSendHistoryItemData NativeSendHistoryState::itemDataForIndex(std::size_t index) const noexcept {
    return static_cast<NativeSendHistoryItemData>(index + 1);
}

std::optional<std::size_t> NativeSendHistoryState::indexFromItemData(NativeSendHistoryItemData itemData) const noexcept {
    if (itemData <= 0) {
        return std::nullopt;
    }

    const std::size_t index = static_cast<std::size_t>(itemData - 1);
    if (index >= entries_.size()) {
        return std::nullopt;
    }
    return index;
}

std::optional<native_storage::SendHistoryEntry> NativeSendHistoryState::entryFromItemData(NativeSendHistoryItemData itemData) const {
    const std::optional<std::size_t> index = indexFromItemData(itemData);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return entries_[*index];
}

native_storage::SendHistoryEntry nativeMakeSendHistoryEntry(
    std::string content,
    int payloadMode,
    int lineEnding,
    int textEncodingCodePage,
    std::string sentAtUtc) {
    native_storage::SendHistoryEntry entry;
    entry.content = std::move(content);
    entry.payloadMode = payloadMode;
    entry.lineEnding = lineEnding;
    entry.textEncodingCodePage = textEncodingCodePage;
    entry.sentAtUtc = std::move(sentAtUtc);
    return entry;
}

} // namespace svm::win32
