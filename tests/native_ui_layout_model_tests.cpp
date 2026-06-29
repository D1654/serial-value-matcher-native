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
    assert(model.workbench.visibility.pageVisible);
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
    assert(model.workbench.workbenchHeight >= model.metrics.minimumWorkHeight);
    assert(model.workbench.page.width > 0);
    assert(model.workbench.page.height > 0);
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
}

void splitterConstraintsPreserveLogAndWorkbenchRoom() {
    const auto low = svm::win32::calculateNativeMainLayoutModel({1212, 753, 1, 0});
    const auto high = svm::win32::calculateNativeMainLayoutModel({1212, 753, 10000, 0});

    assert(low.workbench.workbenchHeight == low.metrics.minimumWorkHeight);
    assert(high.logPanel.bounds.height >= high.metrics.minimumLogHeight);
    assert(high.workbench.workbenchHeight > low.workbench.workbenchHeight);
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(low));
    assert(svm::win32::nativeMainLayoutModelHasStableGeometry(high));
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

} // namespace

int main() {
    defaultWindowLayoutIsStable();
    compactMinimumWindowLayoutIsStable();
    tinyRequestedSizeIsClampedToMinimumModel();
    splitterConstraintsPreserveLogAndWorkbenchRoom();
    statusAndHelpStayAboveWindowBottom();

    std::cout << "native_ui_layout_model_tests passed\n";
    return 0;
}

#endif

