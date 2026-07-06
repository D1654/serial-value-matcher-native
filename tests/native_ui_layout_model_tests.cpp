#if defined(_WIN32)

#include "win32/native_layout_model.h"

#include <cassert>
#include <iostream>

namespace {

bool rectInside(const svm::win32::NativeRect& inner, const svm::win32::NativeRect& outer) {
    return inner.x >= outer.x
        && inner.y >= outer.y
        && inner.right() <= outer.right()
        && inner.bottom() <= outer.bottom();
}

void assertSplitLimits(const svm::win32::NativeMainLayoutModel& model) {
    assert(model.workbench.minimumWorkbenchHeight <= model.workbench.maximumWorkbenchHeight);
    assert(model.workbench.workbenchHeight >= model.workbench.minimumWorkbenchHeight);
    assert(model.workbench.workbenchHeight <= model.workbench.maximumWorkbenchHeight);
    assert(model.workbench.minimumLogHeight == model.metrics.minimumLogHeight);
    assert(model.workbench.logHeight == model.logPanel.bounds.height);
    assert(model.workbench.splitter.bottom() == model.workbench.bounds.y);
}

void assertWorkbenchPageIsUsable(const svm::win32::NativeMainLayoutModel& model) {
    assert(model.workbench.visibility.pageVisible);
    assert(rectInside(model.workbench.pageBackground, model.workbench.tabs));
    assert(rectInside(model.workbench.page, model.workbench.pageBackground));
    assert(model.workbench.page.width >= 1);
    assert(model.workbench.page.height >= 1);
}

void defaultWindowLayoutIsStable() {
    const auto model = svm::win32::calculateNativeMainLayoutModel({1212, 753, 0, 0});

    assert(model.requestedWidth == 1212);
    assert(model.requestedHeight == 753);
    assert(model.width == 1212);
    assert(model.height == 753);
    assert(!model.forcedSmall);
    assert(!model.metrics.compact);
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(model));

    assert(model.logPanel.titleVisible);
    assert(model.logPanel.bounds.height >= model.metrics.minimumLogHeight);
    assert(model.workbench.workbenchHeight == model.metrics.desiredWorkHeight);
    assert(model.workbench.splitter.height == model.metrics.splitterHeight);
    assertSplitLimits(model);
    assertWorkbenchPageIsUsable(model);
    assert(model.workbench.visibility.singleFormatRow);
    assert(model.workbench.visibility.singleSend);
    assert(model.sideHelp.visible);
    assert(rectInside(model.sideHelp.title, model.sideHelp.frame));
    assert(rectInside(model.sideHelp.text, model.sideHelp.frame));
    assert(model.status.clockVisible);
    assert(model.status.countersVisible);
}

void compactMinimumWindowLayoutIsStable() {
    const auto model = svm::win32::calculateNativeMainLayoutModel({760, 520, 0, 2});

    assert(model.width == 760);
    assert(model.height == 520);
    assert(!model.forcedSmall);
    assert(model.metrics.compact);
    assert(model.metrics.tight);
    assert(model.workbench.selectedTabIndex == 2);
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(model));
    assert(model.logPanel.bounds.height >= model.metrics.minimumLogHeight);
    assertSplitLimits(model);
    assertWorkbenchPageIsUsable(model);
    assert(model.status.statusText.width > 0);
}

void tinyRequestedSizeIsClampedToMinimumModel() {
    const auto model = svm::win32::calculateNativeMainLayoutModel({1, 1, 0, -7});

    assert(model.requestedWidth == 1);
    assert(model.requestedHeight == 1);
    assert(model.width == svm::win32::kMinLayoutWidth);
    assert(model.height == svm::win32::kMinLayoutHeight);
    assert(model.forcedSmall);
    assert(model.workbench.selectedTabIndex == 0);
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(model));
    assertSplitLimits(model);
    assertWorkbenchPageIsUsable(model);
}

void splitterConstraintsPreserveLogAndWorkbenchRoom() {
    const auto low = svm::win32::calculateNativeMainLayoutModel({1212, 753, 1, 0});
    const auto high = svm::win32::calculateNativeMainLayoutModel({1212, 753, 10000, 0});

    assert(low.workbench.workbenchHeight == low.workbench.minimumWorkbenchHeight);
    assert(high.workbench.workbenchHeight == high.workbench.maximumWorkbenchHeight);
    assert(high.logPanel.bounds.height == high.workbench.minimumLogHeight);
    assert(high.workbench.workbenchHeight > low.workbench.workbenchHeight);
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(low));
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(high));
    assertSplitLimits(low);
    assertSplitLimits(high);
}

void statusAndHelpStayAboveWindowBottom() {
    const auto model = svm::win32::calculateNativeMainLayoutModel({1040, 720, 236, 4});

    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(model));
    assert(model.status.statusText.bottom() <= model.height);
    if (model.status.clockVisible) {
        assert(model.status.clockText.y == model.status.statusText.y);
        assert(model.status.clockText.bottom() <= model.height);
    }
    if (model.sideHelp.visible) {
        assert(model.sideHelp.frame.bottom() <= model.status.statusText.y);
        assert(rectInside(model.sideHelp.title, model.sideHelp.frame));
        assert(rectInside(model.sideHelp.text, model.sideHelp.frame));
    }
}

void allWorkbenchTabsKeepPageContract() {
    for (int tabIndex = -2; tabIndex <= svm::win32::kNativeWorkbenchTabCount + 1; ++tabIndex) {
        const auto model = svm::win32::calculateNativeMainLayoutModel({1024, 640, 230, tabIndex});

        assert(svm::win32::nativeMainLayoutModelHasStableGeometry(model));
        assert(model.workbench.selectedTabIndex >= 0);
        assert(model.workbench.selectedTabIndex < svm::win32::kNativeWorkbenchTabCount);
        assertWorkbenchPageIsUsable(model);
        assert(model.workbench.visibility.singleSend);
        assert(model.workbench.visibility.fileFirstRow);
        assert(model.workbench.visibility.settingsRow);
    }
}

void promptAreaFollowsAvailableSideHeight() {
    const auto roomy = svm::win32::calculateNativeMainLayoutModel({1212, 753, 236, 3});
    const auto compact = svm::win32::calculateNativeMainLayoutModel({760, 520, 176, 0});

    assert(roomy.sideHelp.visible);
    assert(rectInside(roomy.sideHelp.title, roomy.sideHelp.frame));
    assert(rectInside(roomy.sideHelp.text, roomy.sideHelp.frame));
    assert(roomy.sideHelp.frame.bottom() <= roomy.status.statusText.y);

    if (compact.sideHelp.visible) {
        assert(rectInside(compact.sideHelp.title, compact.sideHelp.frame));
        assert(rectInside(compact.sideHelp.text, compact.sideHelp.frame));
        assert(compact.sideHelp.frame.bottom() <= compact.status.statusText.y);
    }
}

} // namespace

int main() {
    defaultWindowLayoutIsStable();
    compactMinimumWindowLayoutIsStable();
    tinyRequestedSizeIsClampedToMinimumModel();
    splitterConstraintsPreserveLogAndWorkbenchRoom();
    statusAndHelpStayAboveWindowBottom();
    allWorkbenchTabsKeepPageContract();
    promptAreaFollowsAvailableSideHeight();

    std::cout << "native_ui_layout_model_tests passed\n";
    return 0;
}

#endif
