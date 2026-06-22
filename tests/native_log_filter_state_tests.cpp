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

} // namespace

int main() {
    unchangedFilterDoesNotRequestRebuild();
    changedFilterResetsSearchState();
    findNextIsCaseInsensitiveAndWraps();
    emptyNeedleClearsSearch();

    std::cout << "native_log_filter_state_tests passed\n";
    return 0;
}
