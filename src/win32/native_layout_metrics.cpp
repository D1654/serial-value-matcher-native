#include "win32/native_layout_metrics.h"

#if defined(_WIN32)

#include <algorithm>

namespace svm::win32 {
namespace {

bool rectIsValid(const NativeRect& rect) {
    return rect.width > 0 && rect.height > 0;
}

bool sameRowAndAdjacent(const NativeRect& left, const NativeRect& right, int maxGap) {
    return left.y == right.y
        && left.height == right.height
        && right.x >= left.right()
        && right.x - left.right() <= maxGap;
}

} // namespace

NativeUiMetrics nativeUiMetricsForSize(int width, int height) {
    NativeUiMetrics metrics;
    metrics.compact = width < 1040 || height < 720;
    metrics.tight = width < 860;
    metrics.margin = metrics.tight ? 3 : (metrics.compact ? 4 : 6);
    metrics.row = metrics.compact ? 22 : 23;
    metrics.gap = metrics.tight ? 2 : (metrics.compact ? 3 : 4);
    metrics.labelHeight = metrics.compact ? 15 : 16;
    metrics.titleHeight = metrics.compact ? 16 : 18;
    metrics.statusHeight = metrics.compact ? 20 : 22;
    metrics.sideGap = metrics.tight ? 4 : (metrics.compact ? 5 : 6);
    metrics.smallButtonWidth = metrics.tight ? 44 : (metrics.compact ? 48 : 52);
    metrics.desiredSideWidth = metrics.tight ? 150 : (metrics.compact ? 154 : 170);
    metrics.minSideWidth = metrics.tight ? 112 : 140;
    metrics.desiredWorkHeight = metrics.compact ? 192 : 190;
    metrics.minimumLogHeight = metrics.compact ? 150 : 210;
    metrics.logActionWidth = metrics.compact ? 38 : 42;
    return metrics;
}

SendControlLayout calculateSendControlLayout(int x, int y, int innerWidth, int row, int gap, int labelHeight) {
    const bool compact = innerWidth < 390;
    const int sendModeWidth = compact ? 90 : 110;
    const int sendEncodingWidth = compact ? 58 : 70;
    const int lineEndingWidth = compact ? 50 : 64;
    const int historyWidth = std::max(68, innerWidth - sendModeWidth - sendEncodingWidth - lineEndingWidth - gap * 3);

    SendControlLayout layout;
    int cursor = x;
    layout.modeLabel = {cursor, y, sendModeWidth, labelHeight};
    layout.modeCombo = {cursor, y + labelHeight, sendModeWidth, row};
    cursor += sendModeWidth + gap;
    layout.encodingLabel = {cursor, y, sendEncodingWidth, labelHeight};
    layout.encodingCombo = {cursor, y + labelHeight, sendEncodingWidth, row};
    cursor += sendEncodingWidth + gap;
    layout.lineEndingLabel = {cursor, y, lineEndingWidth, labelHeight};
    layout.lineEndingCombo = {cursor, y + labelHeight, lineEndingWidth, row};
    cursor += lineEndingWidth + gap;
    layout.historyLabel = {cursor, y, historyWidth, labelHeight};
    layout.historyCombo = {cursor, y + labelHeight, historyWidth, row};
    return layout;
}

LogToolbarLayout calculateLogToolbarLayout(int x, int y, int innerWidth, int row, int gap, int preferredActionWidth) {
    const bool compact = innerWidth < 620;
    const int usableWidth = std::max(7, innerWidth - gap * 6);
    const int formatPreferredWidth = compact ? 86 : 94;
    const int encodingPreferredWidth = compact ? 60 : 66;
    const int formatMinimumWidth = compact ? 80 : 88;
    const int encodingMinimumWidth = compact ? 56 : 60;
    const int editMinimumWidth = compact ? 34 : 38;
    const int actionBudget = std::max(
        1,
        (usableWidth - formatMinimumWidth - encodingMinimumWidth - editMinimumWidth * 2) / 3);
    const int actionWidth = std::max(1, std::min(preferredActionWidth, actionBudget));
    const int selectorBudget = usableWidth - actionWidth * 3 - editMinimumWidth * 2;
    int formatWidth = formatPreferredWidth;
    int encodingWidth = encodingPreferredWidth;
    if (selectorBudget < formatWidth + encodingWidth) {
        int deficit = formatWidth + encodingWidth - selectorBudget;
        const int formatShrink = std::min(deficit, formatWidth - formatMinimumWidth);
        formatWidth -= formatShrink;
        deficit -= formatShrink;
        const int encodingShrink = std::min(deficit, encodingWidth - encodingMinimumWidth);
        encodingWidth -= encodingShrink;
        deficit -= encodingShrink;
        if (deficit > 0) {
            const int tightSelectorBudget = std::max(2, selectorBudget);
            formatWidth = std::max(1, tightSelectorBudget * 64 / 100);
            encodingWidth = std::max(1, tightSelectorBudget - formatWidth);
        }
    }
    const int variableWidth = std::max(2, usableWidth - formatWidth - encodingWidth - actionWidth * 3);
    const int searchPreferredWidth = compact ? 96 : 110;
    const int searchBaseWidth = std::min(searchPreferredWidth, variableWidth * 42 / 100);
    const int searchWidth = variableWidth >= editMinimumWidth * 2
        ? std::clamp(searchBaseWidth, editMinimumWidth, variableWidth - editMinimumWidth)
        : std::clamp(searchBaseWidth, 1, variableWidth - 1);
    const int filterWidth = variableWidth - searchWidth;

    LogToolbarLayout layout;
    int cursor = x;
    layout.formatLabel = {x, y, 0, 0};
    layout.formatCombo = {cursor, y, formatWidth, row};
    cursor += formatWidth + gap;
    layout.encodingLabel = {cursor, y, 0, 0};
    layout.encodingCombo = {cursor, y, encodingWidth, row};
    cursor += encodingWidth + gap;
    layout.filterLabel = {cursor, y, 0, 0};
    layout.filterEdit = {cursor, y, filterWidth, row};
    cursor += filterWidth + gap;
    layout.searchLabel = {cursor, y, 0, 0};
    layout.searchEdit = {cursor, y, searchWidth, row};
    cursor += searchWidth + gap;
    layout.findButton = {cursor, y, actionWidth, row};
    cursor += actionWidth + gap;
    layout.copyButton = {cursor, y, actionWidth, row};
    cursor += actionWidth + gap;
    layout.exportButton = {cursor, y, actionWidth, row};
    return layout;
}

bool logToolbarLayoutIsSane(int innerWidth) {
    const NativeUiMetrics metrics = nativeUiMetricsForSize(innerWidth + 260, 520);
    const LogToolbarLayout layout = calculateLogToolbarLayout(0, 0, innerWidth, metrics.row, metrics.gap, metrics.logActionWidth);
    return rectIsValid(layout.formatCombo)
        && rectIsValid(layout.encodingCombo)
        && rectIsValid(layout.filterEdit)
        && rectIsValid(layout.searchEdit)
        && rectIsValid(layout.findButton)
        && layout.exportButton.right() <= innerWidth
        && layout.findButton.right() <= innerWidth
        && layout.encodingCombo.right() + metrics.gap <= layout.filterEdit.x
        && sameRowAndAdjacent(layout.filterEdit, layout.searchEdit, metrics.gap)
        && sameRowAndAdjacent(layout.searchEdit, layout.findButton, metrics.gap);
}

bool sendControlLayoutIsSane(int innerWidth) {
    const NativeUiMetrics metrics = nativeUiMetricsForSize(innerWidth + 260, 520);
    const SendControlLayout layout = calculateSendControlLayout(0, 0, innerWidth, metrics.row, metrics.gap, metrics.labelHeight);
    return rectIsValid(layout.modeCombo)
        && rectIsValid(layout.encodingCombo)
        && rectIsValid(layout.lineEndingCombo)
        && rectIsValid(layout.historyCombo)
        && layout.historyCombo.right() <= innerWidth;
}

bool scanTabLayoutIsSane(int pageWidth, int pageHeight) {
    const NativeUiMetrics metrics = nativeUiMetricsForSize(pageWidth + 200, 520);
    const int row = metrics.row;
    const int gap = metrics.gap;
    const int labelHeight = metrics.labelHeight;
    const int formFieldGap = metrics.compact ? 2 : 3;
    const int scanSectionGap = metrics.compact ? 2 : 3;
    const int scanBlockGap = metrics.compact ? 3 : 4;

    const int shortLabelWidth = metrics.compact ? 28 : 32;
    const int scanSlaveEditWidth = metrics.compact ? 34 : 40;
    const int scanFunctionLabelWidth = metrics.compact ? 32 : 36;
    const int addressLabelWidth = metrics.compact ? 52 : 58;
    const int addressEditWidth = metrics.compact ? 46 : 52;
    const int scanFunctionWidth = metrics.compact ? 144 : 168;
    const int parameterRowWidth =
        shortLabelWidth + scanSlaveEditWidth
        + scanFunctionLabelWidth + scanFunctionWidth
        + (addressLabelWidth + addressEditWidth) * 2
        + gap * 4
        + formFieldGap * 4;

    const int targetNameLabelWidth = metrics.compact ? 42 : 48;
    const int targetValueLabelWidth = metrics.compact ? 42 : 48;
    const int targetUnitLabelWidth = metrics.compact ? 28 : 32;
    const int toleranceLabelWidth = metrics.compact ? 28 : 32;
    const int targetNameEditWidth = metrics.compact ? 100 : 126;
    const int targetValueEditWidth = metrics.compact ? 84 : 96;
    const int targetUnitEditWidth = metrics.compact ? 52 : 64;
    const int toleranceEditWidth = metrics.compact ? 58 : 68;
    const int targetRowWidth =
        targetNameLabelWidth + targetNameEditWidth
        + targetValueLabelWidth + targetValueEditWidth
        + targetUnitLabelWidth + targetUnitEditWidth
        + toleranceLabelWidth + toleranceEditWidth
        + gap * 3
        + formFieldGap * 4;

    const int candidateLabelWidth = metrics.compact ? 32 : 36;
    const int modbusButtonWidth = metrics.compact ? 94 : 102;
    const int analysisButtonWidth = metrics.compact ? 66 : 76;
    const int ruleButtonWidth = metrics.compact ? 66 : 76;
    const int exportButtonWidth = metrics.compact ? 66 : 76;
    const int candidateMinimumWidth =
        candidateLabelWidth + 120
        + modbusButtonWidth + analysisButtonWidth + ruleButtonWidth + exportButtonWidth
        + gap * 5
        + formFieldGap;

    const int requiredHeight =
        labelHeight + scanSectionGap
        + row + scanBlockGap
        + row + scanBlockGap
        + labelHeight + scanSectionGap
        + row + scanBlockGap
        + row;
    return pageWidth >= parameterRowWidth
        && pageWidth >= targetRowWidth
        && pageWidth >= candidateMinimumWidth
        && pageHeight >= requiredHeight;
}

MainLayoutProbe calculateMainLayoutProbe(int requestedWidth, int requestedHeight) {
    MainLayoutProbe probe;
    probe.requestedWidth = requestedWidth;
    probe.requestedHeight = requestedHeight;
    probe.width = std::max(requestedWidth, kMinLayoutWidth);
    probe.height = std::max(requestedHeight, kMinLayoutHeight);
    probe.forcedSmall = requestedWidth < kMinLayoutWidth || requestedHeight < kMinLayoutHeight;
    const NativeUiMetrics metrics = nativeUiMetricsForSize(probe.width, probe.height);
    probe.compact = metrics.compact;
    probe.margin = metrics.margin;
    probe.groupPad = 0;
    probe.row = metrics.row;

    probe.statusY = probe.height - metrics.statusHeight - 4;
    probe.rightWidth = metrics.desiredSideWidth;
    probe.leftWidth = std::max(360, probe.width - probe.margin * 2 - metrics.sideGap - probe.rightWidth);
    probe.connectionWidth = probe.rightWidth;
    probe.connectionHeight = std::max(240, probe.statusY - probe.margin);
    probe.contentY = probe.margin;
    probe.contentHeight = std::max(280, probe.statusY - probe.margin);

    probe.sendHeight = metrics.desiredWorkHeight;
    probe.workflowY = probe.margin;
    probe.workflowHeight = std::max(150, probe.contentHeight - probe.sendHeight - metrics.sideGap);
    probe.sendInnerWidth = probe.leftWidth - probe.groupPad * 2;
    probe.logInnerWidth = probe.leftWidth - probe.groupPad * 2;

    const LogToolbarLayout logLayout = calculateLogToolbarLayout(0, 0, probe.logInnerWidth, probe.row, metrics.gap, metrics.logActionWidth);
    const int logContentY = std::max(logLayout.filterEdit.bottom(), logLayout.findButton.bottom()) + (probe.compact ? 5 : 6);
    probe.logContentHeight = std::max(120, probe.workflowHeight - logContentY - probe.groupPad);
    return probe;
}

bool mainLayoutProbeHasStableGeometry(const MainLayoutProbe& probe) {
    return probe.width >= kMinLayoutWidth
        && probe.height >= kMinLayoutHeight
        && probe.connectionWidth > 0
        && probe.contentY >= probe.margin
        && probe.contentHeight > 0
        && probe.leftWidth > 0
        && probe.rightWidth > 0
        && probe.sendInnerWidth > 0
        && probe.logInnerWidth > 0
        && probe.workflowHeight > 0
        && probe.logContentHeight > 0
        && sendControlLayoutIsSane(probe.sendInnerWidth)
        && logToolbarLayoutIsSane(probe.logInnerWidth);
}

bool mainLayoutProbeSupportsFullInteraction(const MainLayoutProbe& probe) {
    return !probe.forcedSmall
        && mainLayoutProbeHasStableGeometry(probe)
        && probe.workflowY + probe.workflowHeight <= probe.statusY
        && probe.contentY + probe.contentHeight <= probe.statusY;
}

bool mainLayoutProbeIsStableAtSize(int width, int height) {
    return mainLayoutProbeHasStableGeometry(calculateMainLayoutProbe(width, height));
}

bool mainLayoutProbeIsFullyUsableAtSize(int width, int height) {
    return mainLayoutProbeSupportsFullInteraction(calculateMainLayoutProbe(width, height));
}

} // namespace svm::win32

#endif
