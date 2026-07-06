#include "win32/native_workbench_tab_state.h"

#include <algorithm>

namespace svm::win32 {
namespace {

inline constexpr int kWorkbenchTabCount = 5;

} // namespace

int nativeNormalizeWorkbenchTabIndex(int tabIndex) noexcept {
    return std::clamp(tabIndex, 0, kWorkbenchTabCount - 1);
}

std::uint64_t NativeWorkbenchTabState::layoutRevision() const noexcept {
    return layoutRevision_;
}

std::uint64_t NativeWorkbenchTabState::appliedRevision() const noexcept {
    return appliedRevision_;
}

std::uint64_t NativeWorkbenchTabState::applyCount() const noexcept {
    return applyCount_;
}

int NativeWorkbenchTabState::activeTabIndex() const noexcept {
    return activeTabIndex_;
}

void NativeWorkbenchTabState::noteLayoutChanged() noexcept {
    ++layoutRevision_;
}

NativeWorkbenchTabApplyPlan NativeWorkbenchTabState::beginApply(
    int tabIndex,
    bool visibilityReady,
    bool pageVisible,
    bool canRedraw,
    bool suspendRedraw) noexcept {
    const int normalizedTabIndex = nativeNormalizeWorkbenchTabIndex(tabIndex);
    if (activeTabIndex_ == normalizedTabIndex
        && appliedRevision_ == layoutRevision_
        && appliedVisibilityReady_ == visibilityReady
        && appliedPageVisible_ == pageVisible) {
        return {.skip = true};
    }

    ++applyCount_;
    NativeWorkbenchTabApplyPlan plan;
    plan.layoutChanged = appliedRevision_ != layoutRevision_;
    plan.hideAllControls = plan.layoutChanged;
    plan.hidePreviousTab = !plan.layoutChanged && activeTabIndex_ >= 0 && activeTabIndex_ != normalizedTabIndex;
    plan.previousTabIndex = activeTabIndex_;
    plan.showRequestedTab = visibilityReady && pageVisible;
    plan.redrawAfterApply = plan.layoutChanged && !suspendRedraw && canRedraw;
    return plan;
}

void NativeWorkbenchTabState::finishApply(int tabIndex, bool visibilityReady, bool pageVisible) noexcept {
    activeTabIndex_ = nativeNormalizeWorkbenchTabIndex(tabIndex);
    appliedRevision_ = layoutRevision_;
    appliedVisibilityReady_ = visibilityReady;
    appliedPageVisible_ = pageVisible;
}

NativeWorkbenchHelpTopic NativeWorkbenchTabState::helpTopicForTab(int tabIndex) const noexcept {
    switch (nativeNormalizeWorkbenchTabIndex(tabIndex)) {
    case 1:
        return NativeWorkbenchHelpTopic::Quick;
    case 2:
        return NativeWorkbenchHelpTopic::File;
    case 3:
        return NativeWorkbenchHelpTopic::Scan;
    case 4:
        return NativeWorkbenchHelpTopic::Settings;
    default:
        return NativeWorkbenchHelpTopic::Single;
    }
}

bool NativeWorkbenchTabState::shouldUpdateHelp(int tabIndex) const noexcept {
    return lastHelpTabIndex_ != tabIndex;
}

void NativeWorkbenchTabState::markHelpUpdated(int tabIndex) noexcept {
    lastHelpTabIndex_ = tabIndex;
}

} // namespace svm::win32
