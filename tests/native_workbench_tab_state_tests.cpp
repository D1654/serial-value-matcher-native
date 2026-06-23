#include "win32/native_workbench_tab_state.h"

#include <cassert>
#include <iostream>

namespace {

void firstApplyShowsRequestedTab() {
    svm::win32::NativeWorkbenchTabState state;
    const auto plan = state.beginApply(0, true, true, true, false);
    assert(!plan.skip);
    assert(!plan.layoutChanged);
    assert(!plan.hideAllControls);
    assert(!plan.hidePreviousTab);
    assert(plan.previousTabIndex == -1);
    assert(plan.showRequestedTab);
    assert(!plan.redrawAfterApply);
    assert(state.applyCount() == 1);

    state.finishApply(0);
    assert(state.activeTabIndex() == 0);
    assert(state.appliedRevision() == state.layoutRevision());
}

void repeatedApplyIsSkipped() {
    svm::win32::NativeWorkbenchTabState state;
    const auto first = state.beginApply(0, true, true, true, false);
    assert(!first.skip);
    state.finishApply(0);

    const auto second = state.beginApply(0, true, true, true, false);
    assert(second.skip);
    assert(state.applyCount() == 1);
}

void layoutChangeHidesAllControlsAndMayRequestRedraw() {
    svm::win32::NativeWorkbenchTabState state;
    const auto first = state.beginApply(0, true, true, true, false);
    assert(!first.skip);
    state.finishApply(0);

    state.noteLayoutChanged();
    const auto plan = state.beginApply(0, true, true, true, false);
    assert(!plan.skip);
    assert(plan.layoutChanged);
    assert(plan.hideAllControls);
    assert(!plan.hidePreviousTab);
    assert(plan.showRequestedTab);
    assert(plan.redrawAfterApply);
    assert(state.applyCount() == 2);
}

void switchingTabsHidesPreviousTabOnly() {
    svm::win32::NativeWorkbenchTabState state;
    const auto first = state.beginApply(0, true, true, true, false);
    assert(!first.skip);
    state.finishApply(0);

    const auto plan = state.beginApply(3, true, true, true, false);
    assert(!plan.skip);
    assert(!plan.layoutChanged);
    assert(!plan.hideAllControls);
    assert(plan.hidePreviousTab);
    assert(plan.previousTabIndex == 0);
    assert(plan.showRequestedTab);
    state.finishApply(3);
    assert(state.activeTabIndex() == 3);
}

void hiddenPageStillRecordsAppliedTab() {
    svm::win32::NativeWorkbenchTabState state;
    const auto plan = state.beginApply(2, false, false, true, false);
    assert(!plan.skip);
    assert(!plan.showRequestedTab);
    state.finishApply(2);
    assert(state.activeTabIndex() == 2);
}

void helpTopicAndUpdateStateTrackTabs() {
    svm::win32::NativeWorkbenchTabState state;
    assert(state.helpTopicForTab(0) == svm::win32::NativeWorkbenchHelpTopic::Single);
    assert(state.helpTopicForTab(1) == svm::win32::NativeWorkbenchHelpTopic::Quick);
    assert(state.helpTopicForTab(2) == svm::win32::NativeWorkbenchHelpTopic::File);
    assert(state.helpTopicForTab(3) == svm::win32::NativeWorkbenchHelpTopic::Scan);
    assert(state.helpTopicForTab(4) == svm::win32::NativeWorkbenchHelpTopic::Settings);
    assert(state.helpTopicForTab(99) == svm::win32::NativeWorkbenchHelpTopic::Single);

    assert(state.shouldUpdateHelp(3));
    state.markHelpUpdated(3);
    assert(!state.shouldUpdateHelp(3));
    assert(state.shouldUpdateHelp(4));
}

} // namespace

int main() {
    firstApplyShowsRequestedTab();
    repeatedApplyIsSkipped();
    layoutChangeHidesAllControlsAndMayRequestRedraw();
    switchingTabsHidesPreviousTabOnly();
    hiddenPageStillRecordsAppliedTab();
    helpTopicAndUpdateStateTrackTabs();

    std::cout << "native_workbench_tab_state_tests passed\n";
    return 0;
}
