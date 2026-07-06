#pragma once

#include <cstdint>

namespace svm::win32 {

enum class NativeWorkbenchHelpTopic {
    Single,
    Quick,
    File,
    Scan,
    Settings,
};

struct NativeWorkbenchTabApplyPlan {
    bool skip = false;
    bool layoutChanged = false;
    bool hideAllControls = false;
    bool hidePreviousTab = false;
    int previousTabIndex = -1;
    bool showRequestedTab = false;
    bool redrawAfterApply = false;
};

int nativeNormalizeWorkbenchTabIndex(int tabIndex) noexcept;

class NativeWorkbenchTabState final {
public:
    std::uint64_t layoutRevision() const noexcept;
    std::uint64_t appliedRevision() const noexcept;
    std::uint64_t applyCount() const noexcept;
    int activeTabIndex() const noexcept;

    void noteLayoutChanged() noexcept;
    NativeWorkbenchTabApplyPlan beginApply(
        int tabIndex,
        bool visibilityReady,
        bool pageVisible,
        bool canRedraw,
        bool suspendRedraw) noexcept;
    void finishApply(int tabIndex, bool visibilityReady = true, bool pageVisible = true) noexcept;

    NativeWorkbenchHelpTopic helpTopicForTab(int tabIndex) const noexcept;
    bool shouldUpdateHelp(int tabIndex) const noexcept;
    void markHelpUpdated(int tabIndex) noexcept;

private:
    int activeTabIndex_ = -1;
    int lastHelpTabIndex_ = -1;
    bool appliedVisibilityReady_ = false;
    bool appliedPageVisible_ = false;
    std::uint64_t layoutRevision_ = 0;
    std::uint64_t appliedRevision_ = 0;
    std::uint64_t applyCount_ = 0;
};

} // namespace svm::win32
