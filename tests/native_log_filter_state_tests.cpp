#include "win32/native_log_model.h"

#include <cassert>
#include <iostream>

namespace {

using svm::win32::NativeLogFilterState;

void unchangedFilterDoesNotRequestRebuild() {
    NativeLogFilterState state;
    auto update = state.setFilterText(L"TX");
    assert(update.changed);
    assert(update.filterText == L"TX");

    update = state.setFilterText(L"TX");
    assert(!update.changed);
    assert(update.filterText == L"TX");
}

void changedFilterResetsSearchState() {
    NativeLogFilterState state;
    state.setFilterText(L"TX");
    const auto first = state.findNext(L"abc tx abc tx", L"tx");
    assert(first.found);
    assert(state.searchOffset() > 0);

    const auto update = state.setFilterText(L"RX");
    assert(update.changed);
    assert(state.filterText() == L"RX");
    assert(state.loweredFilterText() == L"rx");
    assert(state.searchText().empty());
    assert(state.searchOffset() == 0);
}

void findNextIsCaseInsensitiveAndWraps() {
    NativeLogFilterState state;
    const std::wstring text = L"[TX] One\r\n[rx] two\r\n[TX] three\r\n";

    auto match = state.findNext(text, L"tx");
    assert(match.found);
    assert(match.position == 1);
    assert(match.length == 2);
    assert(!match.wrapped);

    match = state.findNext(text, L"tx");
    assert(match.found);
    assert(match.position > 1);
    assert(!match.wrapped);

    match = state.findNext(text, L"tx");
    assert(match.found);
    assert(match.position == 1);
    assert(match.wrapped);
}

void emptyNeedleClearsSearch() {
    NativeLogFilterState state;
    assert(state.findNext(L"abc", L"a").found);
    const auto empty = state.findNext(L"abc", L"");
    assert(!empty.found);
    assert(state.searchText().empty());
    assert(state.searchOffset() == 0);
}

void visibleLogTextEscapesControlCharacters() {
    std::wstring text;
    text.push_back(L'A');
    text.push_back(L'\r');
    text.push_back(L'\n');
    text.push_back(L'\t');
    text.push_back(static_cast<wchar_t>(0x01));
    text.push_back(L'B');

    assert(svm::win32::sanitizeLogText(text) == L"A\\r\\n\\t.B");
}

void containsNeedleIsCaseInsensitive() {
    assert(svm::win32::containsCaseInsensitive(L"[RX] Temperature=42", L"rx"));
    assert(svm::win32::containsCaseInsensitive(L"[TX] 温度=42", L"tx"));
    assert(!svm::win32::containsCaseInsensitive(L"[系统] connected", L"error"));
}

void longVisibleLogLinesAreClippedWithSuffix() {
    const std::wstring clipped = svm::win32::clipRenderedLogLine(L"ABCDEFGHIJ", 7, L"...");
    assert(clipped == L"ABCD...");

    const std::wstring unchanged = svm::win32::clipRenderedLogLine(L"ABC", 7, L"...");
    assert(unchanged == L"ABC");
}

void logEntriesAreBuiltByTheModel() {
    const auto text = svm::win32::nativeMakeTextLogEntry(
        svm::win32::NativeLogKind::Error,
        L"10:00",
        L"failed");
    assert(text.kind == svm::win32::NativeLogKind::Error);
    assert(text.timestamp == L"10:00");
    assert(text.text == L"failed");
    assert(!text.hasPayload);

    const auto payload = svm::win32::nativeMakePayloadLogEntry(
        svm::win32::NativeLogKind::Tx,
        L"10:01",
        {0x41, 0x42});
    assert(payload.kind == svm::win32::NativeLogKind::Tx);
    assert(payload.timestamp == L"10:01");
    assert(payload.payloadPrefix == L"[TX]");
    assert(payload.payload.size() == 2);
    assert(payload.hasPayload);
}

} // namespace

int main() {
    unchangedFilterDoesNotRequestRebuild();
    changedFilterResetsSearchState();
    findNextIsCaseInsensitiveAndWraps();
    emptyNeedleClearsSearch();
    visibleLogTextEscapesControlCharacters();
    containsNeedleIsCaseInsensitive();
    longVisibleLogLinesAreClippedWithSuffix();
    logEntriesAreBuiltByTheModel();

    std::cout << "native_log_filter_state_tests passed\n";
    return 0;
}
