#include "win32/native_layout_model.h"

#if defined(_WIN32)

#include <algorithm>
#include <cstddef>

namespace svm::win32 {
namespace {

bool rectIsValid(const NativeRect& rect) noexcept {
    return rect.width > 0 && rect.height > 0;
}

NativeRect makeRectFromEdges(int left, int top, int right, int bottom) noexcept {
    return {
        left,
        top,
        std::max(1, right - left),
        std::max(1, bottom - top),
    };
}

int normalizeTabIndex(int tabIndex) noexcept {
    return std::clamp(tabIndex, 0, kNativeWorkbenchTabCount - 1);
}

} // namespace

NativeMainLayoutModel calculateNativeMainLayoutModel(const NativeLayoutModelInput& input) {
    NativeMainLayoutModel model;
    model.requestedWidth = input.clientWidth;
    model.requestedHeight = input.clientHeight;
    model.width = std::max(input.clientWidth, kMinLayoutWidth);
    model.height = std::max(input.clientHeight, kMinLayoutHeight);
    model.forcedSmall = model.width != input.clientWidth || model.height != input.clientHeight;
    model.metrics = nativeUiMetricsForSize(model.width, model.height);

    const NativeUiMetrics& metrics = model.metrics;
    const int margin = metrics.margin;
    const int gap = metrics.gap;
    const int row = metrics.row;
    const int labelHeight = metrics.labelHeight;
    const bool compact = metrics.compact;

    const int statusHeight = metrics.statusHeight;
    const int statusY = std::max(margin, model.height - statusHeight - 4);
    const int availableWidth = std::max(1, model.width - margin * 2);
    const int maxSideWidth = std::min(metrics.desiredSideWidth, std::max(1, availableWidth - 80));
    const int minSideWidth = std::min(metrics.minSideWidth, maxSideWidth);
    const int sideWidth = std::clamp(
        std::min(metrics.desiredSideWidth, availableWidth / 4),
        minSideWidth,
        maxSideWidth);
    const int sideX = model.width - margin - sideWidth;
    const int mainX = margin;
    const int mainWidth = std::max(1, sideX - metrics.sideGap - mainX);
    const int contentHeight = std::max(1, statusY - margin);

    const int requestedWorkHeight = input.requestedWorkbenchHeight > 0
        ? input.requestedWorkbenchHeight
        : metrics.desiredWorkHeight;
    const int maximumWorkHeight = std::max(84, contentHeight - metrics.minimumLogHeight - metrics.splitterHeight);
    const int minimumWorkHeight = std::min(std::max(84, metrics.minimumWorkHeight), maximumWorkHeight);
    const int workHeight = clampedWorkbenchHeightForContent(requestedWorkHeight, metrics, contentHeight);
    const int logY = margin;
    const int logHeight = std::max(1, statusY - logY - workHeight - metrics.splitterHeight);
    const int splitterY = logY + logHeight;
    const int workY = splitterY + metrics.splitterHeight;
    const int workHeightAvailable = std::max(1, statusY - workY - 2);

    model.mainArea = {mainX, margin, mainWidth, contentHeight};
    model.serialPanel.bounds = {sideX, margin, sideWidth, contentHeight};
    model.logPanel.bounds = {mainX, logY, mainWidth, logHeight};
    model.workbench.bounds = {mainX, workY, mainWidth, workHeightAvailable};
    model.workbench.splitter = {mainX, splitterY, mainWidth, metrics.splitterHeight};
    model.workbench.minimumWorkbenchHeight = minimumWorkHeight;
    model.workbench.maximumWorkbenchHeight = maximumWorkHeight;
    model.workbench.minimumLogHeight = metrics.minimumLogHeight;
    model.workbench.workbenchHeight = workHeight;
    model.workbench.logHeight = logHeight;
    model.workbench.selectedTabIndex = normalizeTabIndex(input.selectedTabIndex);

    const int sideInnerWidth = std::max(1, sideWidth);
    const int sideActionGap = compact ? 2 : 4;
    const int sideSeparatorHeight = 2;
    const int sideLabelWidth = compact ? 44 : 48;
    int sideCursorY = margin + metrics.titleHeight + gap;
    sideCursorY += row + gap; // Port combo.
    sideCursorY += row + gap; // Refresh/save row.
    sideCursorY += (row + gap) * 4; // Baud/stop/data/parity.
    sideCursorY += row + gap; // Flow.
    sideCursorY += row + gap; // Connect.
    sideCursorY += row; // DTR/RTS.
    sideCursorY += row; // Auto reconnect.
    const int sideControlBottom = sideCursorY;
    const int sideActionSeparatorY = sideControlBottom + sideActionGap;
    const int sideActionButtonY = sideActionSeparatorY + sideSeparatorHeight + sideActionGap;
    const int sideActionBottom = sideActionButtonY + row;
    model.serialPanel.actionsVisible = sideInnerWidth >= 108 && sideActionButtonY + row <= statusY - margin;
    model.serialPanel.actionSeparator = {sideX, sideActionSeparatorY, sideInnerWidth, sideSeparatorHeight};
    const int sideActionButtonWidth = std::max(1, (sideInnerWidth - gap) / 2);
    model.serialPanel.pauseButton = {sideX, sideActionButtonY, sideActionButtonWidth, row};
    model.serialPanel.clearButton = {sideX + sideActionButtonWidth + gap, sideActionButtonY, sideActionButtonWidth, row};

    const int sideHelpBottom = statusY - 2;
    const int sideHelpMinimumHeight = compact ? 132 : 150;
    const int sideHelpDesiredHeight = compact ? 152 : 180;
    const int earliestSideHelpY = sideActionBottom + sideActionGap + sideSeparatorHeight + sideActionGap;
    const int sideHelpY = std::max(earliestSideHelpY, sideHelpBottom - sideHelpDesiredHeight);
    const int sideHelpSeparatorY = sideHelpY - sideActionGap - sideSeparatorHeight;
    const int sideHelpHeight = std::max(1, sideHelpBottom - sideHelpY);
    model.sideHelp.visible = model.serialPanel.actionsVisible
        && sideHelpHeight >= sideHelpMinimumHeight
        && sideHelpSeparatorY >= sideActionBottom + sideActionGap
        && sideHelpY + sideHelpHeight <= statusY;
    model.sideHelp.separator = {sideX, sideHelpSeparatorY, sideInnerWidth, sideSeparatorHeight};
    model.sideHelp.frame = {sideX, sideHelpY, sideInnerWidth, sideHelpHeight};
    const int helpPad = compact ? 6 : 7;
    const int helpTitleHeight = compact ? 17 : 18;
    const int helpTitleWidth = compact ? 66 : 74;
    model.sideHelp.title = {
        sideX + helpPad,
        sideHelpY + helpPad,
        std::min(std::max(1, sideInnerWidth - helpPad * 2), helpTitleWidth),
        helpTitleHeight,
    };
    model.sideHelp.text = {
        sideX + helpPad,
        sideHelpY + helpPad + helpTitleHeight + (compact ? 3 : 4),
        std::max(1, sideInnerWidth - helpPad * 2),
        std::max(1, sideHelpHeight - helpPad * 2 - helpTitleHeight - (compact ? 3 : 4)),
    };

    const int logTitleWidth = compact ? 52 : 58;
    model.logPanel.titleVisible = mainWidth >= 520;
    model.logPanel.title = {mainX, logY + 2, logTitleWidth, metrics.titleHeight};
    const int toolbarX = model.logPanel.titleVisible ? mainX + logTitleWidth + gap : mainX;
    const int toolbarWidth = std::max(1, mainWidth - (model.logPanel.titleVisible ? logTitleWidth + gap : 0));
    model.logPanel.toolbar = calculateLogToolbarLayout(toolbarX, logY, toolbarWidth, row, gap, metrics.logActionWidth);
    const int logContentY = std::max(model.logPanel.toolbar.exportButton.bottom(), model.logPanel.toolbar.findButton.bottom()) + (compact ? 5 : 6);
    model.logPanel.logView = {mainX, logContentY, mainWidth, std::max(1, logY + logHeight - logContentY)};

    model.workbench.tabs = {mainX, workY, mainWidth, std::max(84, workHeightAvailable)};
    const int tabInsetLeft = 2;
    const int tabHeaderHeight = compact ? 24 : 25;
    const int pageHorizontalInset = compact ? 7 : 6;
    const int pageVerticalInset = compact ? 3 : 4;
    model.workbench.pageBackground = {
        model.workbench.tabs.x + tabInsetLeft,
        model.workbench.tabs.y + tabHeaderHeight,
        std::max(1, model.workbench.tabs.width - tabInsetLeft * 2),
        std::max(1, model.workbench.tabs.height - tabHeaderHeight - 2),
    };
    model.workbench.page = makeRectFromEdges(
        model.workbench.pageBackground.x + pageHorizontalInset,
        model.workbench.pageBackground.y + pageVerticalInset,
        model.workbench.pageBackground.right() - pageHorizontalInset,
        model.workbench.pageBackground.bottom() - pageVerticalInset);

    const int pageW = model.workbench.page.width;
    const int pageY = model.workbench.page.y;
    const int pageBottom = model.workbench.page.bottom();
    model.workbench.visibility.pageVisible = model.workbench.pageBackground.height > 1;
    model.workbench.visibility.singleFormatRow = pageY + row <= pageBottom;
    model.workbench.visibility.singleHistory = model.workbench.visibility.singleFormatRow && pageW >= 380;
    const int singleSendExtraGap = pageBottom - pageY >= row * 4 ? (compact ? 4 : 8) : 0;
    const int singleContentY = model.workbench.visibility.singleFormatRow ? (pageY + row + gap + singleSendExtraGap) : pageY;
    model.workbench.visibility.singleSend = singleContentY + row <= pageBottom && pageW >= 96;
    model.workbench.visibility.singleTimed = singleContentY + row + gap + row <= pageBottom && pageW >= 230;

    int quickColumns = 2;
    if (pageW >= 620) {
        quickColumns = 5;
    } else if (pageW >= 480) {
        quickColumns = 4;
    } else if (pageW >= 340) {
        quickColumns = 3;
    }
    const int quickColumnWidth = std::max(1, (pageW - gap * (quickColumns - 1)) / quickColumns);
    const int quickButtonWidth = std::max(1, std::min(compact ? 32 : 36, quickColumnWidth / 2));
    for (std::size_t index = 0; index < model.workbench.visibility.quickSlots.size(); ++index) {
        const int slotRow = static_cast<int>(index / quickColumns);
        const int slotY = pageY + slotRow * (row + gap);
        model.workbench.visibility.quickSlots[index] = slotY + row <= pageBottom && quickColumnWidth > quickButtonWidth + gap;
    }

    model.workbench.visibility.fileFirstRow = pageY + row <= pageBottom && pageW >= (compact ? 30 : 34) + (compact ? 44 : 50) + (compact ? 68 : 76) + (compact ? 44 : 50) + gap * 4 + 24;
    model.workbench.visibility.fileSecondRow = pageY + row + gap + row <= pageBottom;
    model.workbench.visibility.scanSection = pageY + labelHeight <= pageBottom;
    const int shortLabelWidth = compact ? 28 : 32;
    const int scanSlaveEditWidth = compact ? 34 : 40;
    const int addressLabelWidth = compact ? 52 : 58;
    const int addressEditWidth = compact ? 46 : 52;
    const int scanFunctionLabelWidth = compact ? 32 : 36;
    const int modbusButtonWidth = compact ? 94 : 102;
    const int formFieldGap = compact ? 2 : 3;
    const int fixedScanRowWidth = shortLabelWidth + scanSlaveEditWidth
        + scanFunctionLabelWidth + addressLabelWidth + addressEditWidth
        + addressLabelWidth + addressEditWidth + modbusButtonWidth
        + gap * 4 + formFieldGap * 4;
    model.workbench.visibility.scanParameterRow = pageY + labelHeight + (compact ? 2 : 3) + row <= pageBottom
        && pageW >= fixedScanRowWidth + 80;
    model.workbench.visibility.scanProgressRow = pageY + labelHeight + (compact ? 2 : 3) + row + (compact ? 3 : 4) + row <= pageBottom
        && pageW >= 180;
    model.workbench.visibility.scanAnalysisSection = pageY + labelHeight + (compact ? 2 : 3) + row + (compact ? 3 : 4) + row + (compact ? 3 : 4) + labelHeight <= pageBottom;
    model.workbench.visibility.scanTargetRow = pageBottom - pageY >= labelHeight * 2 + row * 3;
    model.workbench.visibility.scanCandidateRow = pageBottom - pageY >= labelHeight * 2 + row * 4;
    model.workbench.visibility.settingsRow = pageY + row <= pageBottom;

    int statusRight = model.width - margin;
    model.status.clockVisible = model.width >= 560;
    model.status.countersVisible = model.width >= 470;
    const int clockWidth = compact ? 78 : 88;
    const int counterWidth = compact ? 76 : 86;
    if (model.status.clockVisible) {
        model.status.clockText = {statusRight - clockWidth, statusY, clockWidth, statusHeight};
        statusRight -= clockWidth + gap;
    }
    if (model.status.countersVisible) {
        model.status.rxText = {statusRight - counterWidth, statusY, counterWidth, statusHeight};
        statusRight -= counterWidth + gap;
        model.status.txText = {statusRight - counterWidth, statusY, counterWidth, statusHeight};
        statusRight -= counterWidth + gap;
    }
    model.status.statusText = {margin, statusY, std::max(1, statusRight - margin), statusHeight};

    return model;
}

bool nativeMainLayoutModelHasStableGeometry(const NativeMainLayoutModel& model) {
    return model.width >= kMinLayoutWidth
        && model.height >= kMinLayoutHeight
        && rectIsValid(model.mainArea)
        && rectIsValid(model.serialPanel.bounds)
        && rectIsValid(model.logPanel.bounds)
        && rectIsValid(model.logPanel.logView)
        && rectIsValid(model.workbench.bounds)
        && rectIsValid(model.workbench.splitter)
        && rectIsValid(model.workbench.tabs)
        && rectIsValid(model.workbench.pageBackground)
        && rectIsValid(model.workbench.page)
        && rectIsValid(model.status.statusText)
        && model.workbench.minimumWorkbenchHeight <= model.workbench.maximumWorkbenchHeight
        && model.workbench.workbenchHeight >= model.workbench.minimumWorkbenchHeight
        && model.workbench.workbenchHeight <= model.workbench.maximumWorkbenchHeight
        && model.workbench.minimumLogHeight == model.metrics.minimumLogHeight
        && model.workbench.logHeight == model.logPanel.bounds.height
        && model.logPanel.bounds.bottom() <= model.workbench.splitter.y
        && model.workbench.splitter.bottom() <= model.workbench.bounds.y
        && model.workbench.bounds.bottom() <= model.status.statusText.y + 2
        && model.status.statusText.bottom() <= model.height
        && model.serialPanel.bounds.right() <= model.width
        && model.mainArea.right() < model.serialPanel.bounds.x;
}

} // namespace svm::win32

#endif
