#include "win32/native_send_history_state.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

svm::native_storage::SendHistoryEntry historyEntry(std::string content, int mode) {
    return svm::win32::nativeMakeSendHistoryEntry(std::move(content), mode, 3, 65001, "20260622193000");
}

void itemDataMapsToOneBasedHistoryIndexes() {
    svm::win32::NativeSendHistoryState state;
    state.setEntries({historyEntry("A", 0), historyEntry("B", 1)});

    assert(state.size() == 2);
    assert(!state.empty());
    assert(state.itemDataForIndex(0) == 1);
    assert(state.itemDataForIndex(1) == 2);

    const auto firstIndex = state.indexFromItemData(1);
    assert(firstIndex.has_value());
    assert(*firstIndex == 0);

    const auto second = state.entryFromItemData(2);
    assert(second.has_value());
    assert(second->content == "B");
    assert(second->payloadMode == 1);
}

void invalidItemDataDoesNotSelectHistory() {
    svm::win32::NativeSendHistoryState state;
    state.setEntries({historyEntry("A", 0)});

    assert(!state.indexFromItemData(0).has_value());
    assert(!state.indexFromItemData(-1).has_value());
    assert(!state.indexFromItemData(2).has_value());
    assert(!state.entryFromItemData(2).has_value());
}

void replacingAndClearingEntriesIsExplicit() {
    svm::win32::NativeSendHistoryState state;
    state.setEntries({historyEntry("A", 0), historyEntry("B", 1)});
    state.setEntries({historyEntry("C", 2)});

    assert(state.size() == 1);
    assert(state.entries()[0].content == "C");
    state.clear();
    assert(state.empty());
}

void makeEntryKeepsSendMetadataTogether() {
    const auto entry = svm::win32::nativeMakeSendHistoryEntry("AT", 2, 1, 936, "now");
    assert(entry.content == "AT");
    assert(entry.payloadMode == 2);
    assert(entry.lineEnding == 1);
    assert(entry.textEncodingCodePage == 936);
    assert(entry.sentAtUtc == "now");
}

} // namespace

int main() {
    itemDataMapsToOneBasedHistoryIndexes();
    invalidItemDataDoesNotSelectHistory();
    replacingAndClearingEntriesIsExplicit();
    makeEntryKeepsSendMetadataTogether();

    std::cout << "native_send_history_state_tests passed\n";
    return 0;
}
