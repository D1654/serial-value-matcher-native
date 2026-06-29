#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_layout_metrics.h"
#include "win32/native_ui_preferences.h"

#include <algorithm>
#include <array>
#include <commctrl.h>

namespace svm::win32 {

bool NativeMainWindow::splitterHitTest(int x, int y) const noexcept {
    return workbenchSplitterRect_.right > workbenchSplitterRect_.left
        && workbenchSplitterRect_.bottom > workbenchSplitterRect_.top
        && x >= workbenchSplitterRect_.left
        && x < workbenchSplitterRect_.right
        && y >= workbenchSplitterRect_.top
        && y < workbenchSplitterRect_.bottom;
}

int NativeMainWindow::clampedWorkbenchHeightForClient(int requestedHeight, int width, int height) const {
    width = std::max(width, 1);
    height = std::max(height, 1);
    const NativeUiMetrics metrics = nativeUiMetricsForSize(width, height);
    const int statusY = std::max(metrics.margin, height - metrics.statusHeight - 4);
    const int contentHeight = std::max(1, statusY - metrics.margin);
    return clampedWorkbenchHeightForContent(
        nativeNormalizeWorkbenchHeight(requestedHeight),
        metrics,
        contentHeight);
}

void NativeMainWindow::relayoutCurrentClient(bool immediate) {
    if (window_ == nullptr) {
        return;
    }
    RECT clientRect = {};
    GetClientRect(window_, &clientRect);
    if (!immediate) {
        SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }
    layoutControls(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
    if (!immediate) {
        SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return;
    }
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void NativeMainWindow::paintLayoutChrome() {
    PAINTSTRUCT paint = {};
    HDC dc = BeginPaint(window_, &paint);
    if (dc == nullptr) {
        return;
    }

    if (workbenchSplitterRect_.right > workbenchSplitterRect_.left
        && workbenchSplitterRect_.bottom > workbenchSplitterRect_.top) {
        RECT splitterRect = workbenchSplitterRect_;
        FillRect(dc, &splitterRect, GetSysColorBrush(COLOR_BTNFACE));

        const int splitterHeight = static_cast<int>(splitterRect.bottom - splitterRect.top);
        const int centerY = static_cast<int>(splitterRect.top) + std::max(1, splitterHeight) / 2;
        const int centerX = static_cast<int>(splitterRect.left + splitterRect.right) / 2;
        const int gripHalfWidth = std::min(42, std::max(14, static_cast<int>(splitterRect.right - splitterRect.left) / 12));

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(178, 178, 178));
        HGDIOBJ oldPen = borderPen != nullptr ? SelectObject(dc, borderPen) : nullptr;
        MoveToEx(dc, splitterRect.left + 3, splitterRect.top + 1, nullptr);
        LineTo(dc, splitterRect.right - 3, splitterRect.top + 1);
        MoveToEx(dc, splitterRect.left + 3, splitterRect.bottom - 2, nullptr);
        LineTo(dc, splitterRect.right - 3, splitterRect.bottom - 2);

        HPEN gripPen = CreatePen(PS_SOLID, 1, RGB(118, 118, 118));
        if (gripPen != nullptr) {
            if (oldPen == nullptr) {
                oldPen = SelectObject(dc, gripPen);
            } else {
                SelectObject(dc, gripPen);
            }
        }
        MoveToEx(dc, centerX - gripHalfWidth, centerY - 2, nullptr);
        LineTo(dc, centerX + gripHalfWidth, centerY - 2);
        MoveToEx(dc, centerX - gripHalfWidth, centerY + 2, nullptr);
        LineTo(dc, centerX + gripHalfWidth, centerY + 2);
        if (oldPen != nullptr) {
            SelectObject(dc, oldPen);
        }
        if (gripPen != nullptr) {
            DeleteObject(gripPen);
        }
        if (borderPen != nullptr) {
            DeleteObject(borderPen);
        }
    }

    EndPaint(window_, &paint);
}

void NativeMainWindow::layoutControls(int width, int height) {
    ++layoutPassCount_;

    width = std::max(width, 1);
    height = std::max(height, 1);

    const NativeUiMetrics metrics = nativeUiMetricsForSize(width, height);
    const bool compact = metrics.compact;
    const int margin = metrics.margin;
    const int row = metrics.row;
    const int gap = metrics.gap;
    const int labelHeight = metrics.labelHeight;
    const int titleHeight = metrics.titleHeight;
    const int editHeight = singleLineEditHeight(uiFont_, row);
    const int editOffsetY = std::max(0, (row - editHeight) / 2);
    const BOOL layoutRepaint = draggingWorkbenchSplitter_ ? FALSE : TRUE;

    const int statusHeight = metrics.statusHeight;
    const int statusY = std::max(margin, height - statusHeight - 4);
    const int sideGap = metrics.sideGap;
    const int availableWidth = std::max(1, width - margin * 2);
    const int desiredSideWidth = metrics.desiredSideWidth;
    const int maxSideWidth = std::min(desiredSideWidth, std::max(1, availableWidth - 80));
    const int minSideWidth = std::min(metrics.minSideWidth, maxSideWidth);
    const int sideWidth = std::clamp(std::min(desiredSideWidth, availableWidth / 4), minSideWidth, maxSideWidth);
    const int sideX = width - margin - sideWidth;
    const int mainX = margin;
    const int mainWidth = std::max(1, sideX - sideGap - mainX);
    const int contentHeight = std::max(1, statusY - margin);

    const int minimumLogHeight = metrics.minimumLogHeight;
    const int splitterHeight = metrics.splitterHeight;
    const int requestedWorkHeight = preferredWorkbenchHeight_ > 0 ? preferredWorkbenchHeight_ : metrics.desiredWorkHeight;
    const int workHeight = clampedWorkbenchHeightForContent(requestedWorkHeight, metrics, contentHeight);
    currentWorkbenchHeight_ = workHeight;
    const int logY = margin;
    const int logHeight = std::max(1, statusY - logY - workHeight - splitterHeight);
    const int splitterY = logY + logHeight;
    workbenchSplitterRect_ = {mainX, splitterY, mainX + mainWidth, splitterY + splitterHeight};
    const int sendY = splitterY + splitterHeight;
    const int sendHeight = std::max(1, statusY - sendY - 2);
    const int tabsY = sendY;
    const int tabsHeight = std::max(84, sendHeight);

    showControl(connectionGroup_, false);
    showControl(sendGroup_, false);
    showControl(workflowGroup_, false);
    showControl(logGroup_, false);

    moveControl(serialPanelTitle_, sideX, margin, sideWidth, titleHeight, layoutRepaint);
    int x = sideX;
    int y = margin + titleHeight + gap;
    const int sideInnerWidth = std::max(1, sideWidth);
    const int sideLabelWidth = compact ? 44 : 48;
    const int sideControlWidth = std::max(1, sideInnerWidth - sideLabelWidth - gap);

    showControl(portLabel_, false);
    moveControl(portCombo_, x, y, sideInnerWidth, 180, layoutRepaint);
    y += row + gap;
    moveControl(refreshButton_, x, y, (sideInnerWidth - gap) / 2, row, layoutRepaint);
    moveControl(saveProfileButton_, x + (sideInnerWidth + gap) / 2, y, (sideInnerWidth - gap) / 2, row, layoutRepaint);
    y += row + gap;

    const auto moveSidePair = [&](HWND label, HWND control, int comboDropHeight) {
        moveControl(label, x, y + 4, sideLabelWidth, labelHeight, layoutRepaint);
        moveControl(control, x + sideLabelWidth + gap, y, sideControlWidth, comboDropHeight, layoutRepaint);
        y += row + gap;
    };
    moveSidePair(baudLabel_, baudCombo_, 180);
    moveSidePair(stopBitsLabel_, stopBitsCombo_, 140);
    moveSidePair(dataBitsLabel_, dataBitsCombo_, 140);
    moveSidePair(parityLabel_, parityCombo_, 150);
    moveSidePair(flowControlLabel_, flowControlCombo_, 150);

    moveControl(connectButton_, x, y, sideInnerWidth, row, layoutRepaint);
    showControl(disconnectButton_, false);
    y += row + gap;
    moveControl(dtrCheck_, x, y + 2, sideInnerWidth / 2, row - 2, layoutRepaint);
    moveControl(rtsCheck_, x + sideInnerWidth / 2, y + 2, sideInnerWidth / 2, row - 2, layoutRepaint);
    y += row;
    moveControl(autoReconnectCheck_, x, y + 2, sideInnerWidth, row - 2, layoutRepaint);
    const int sideControlBottom = y + row;
    const int sideSeparatorHeight = 2;
    const int sideActionGap = compact ? 2 : 4;
    const int sideHelpMinimumHeight = compact ? 152 : 170;
    const int sideActionSeparatorY = sideControlBottom + sideActionGap;
    const int sideActionButtonY = sideActionSeparatorY + sideSeparatorHeight + sideActionGap;
    const int sideHelpSeparatorY = sideActionButtonY + row + sideActionGap;
    const int sideHelpY = sideHelpSeparatorY + sideSeparatorHeight + sideActionGap;
    const int sideHelpHeight = std::max(1, statusY - sideHelpY - 2);
    const bool sideActionVisible = sideInnerWidth >= 108
        && sideActionButtonY + row <= statusY - margin;
    moveControl(sideActionSeparator_, x, sideActionSeparatorY, sideInnerWidth, sideSeparatorHeight, layoutRepaint);
    showControl(sideActionSeparator_, sideActionVisible);
    const int sideActionButtonWidth = std::max(1, (sideInnerWidth - gap) / 2);
    moveControl(pauseScrollButton_, x, sideActionButtonY, sideActionButtonWidth, row, layoutRepaint);
    moveControl(clearButton_, x + sideActionButtonWidth + gap, sideActionButtonY, sideActionButtonWidth, row, layoutRepaint);
    showControl(pauseScrollButton_, sideActionVisible);
    showControl(clearButton_, sideActionVisible);
    const bool sideHelpVisible = sideActionVisible
        && sideHelpHeight >= sideHelpMinimumHeight
        && sideHelpY + sideHelpHeight <= statusY;
    moveControl(sideHelpSeparator_, x, sideHelpSeparatorY, sideInnerWidth, sideSeparatorHeight, layoutRepaint);
    showControl(sideHelpSeparator_, sideHelpVisible);
    if (sideHelpVisible) {
        const int helpHeight = sideHelpHeight;
        const int helpY = sideHelpY;
        const int helpPad = compact ? 6 : 7;
        const int helpTitleHeight = compact ? 17 : 18;
        const int helpTitleWidth = compact ? 66 : 74;
        moveControl(sideHelpFrame_, x, helpY, sideInnerWidth, helpHeight, layoutRepaint);
        moveControl(sideHelpTitle_, x + helpPad, helpY + helpPad, std::min(std::max(1, sideInnerWidth - helpPad * 2), helpTitleWidth), helpTitleHeight, layoutRepaint);
        moveControl(
            sideHelpText_,
            x + helpPad,
            helpY + helpPad + helpTitleHeight + (compact ? 3 : 4),
            std::max(1, sideInnerWidth - helpPad * 2),
            std::max(1, helpHeight - helpPad * 2 - helpTitleHeight - (compact ? 3 : 4)),
            layoutRepaint);
    } else {
        moveControl(sideHelpFrame_, x, sideHelpY, 1, 1, layoutRepaint);
        moveControl(sideHelpTitle_, x, sideHelpY, 1, 1, layoutRepaint);
        moveControl(sideHelpText_, x, sideHelpY, 1, 1, layoutRepaint);
    }
    showControl(sideHelpFrame_, sideHelpVisible);
    showControl(sideHelpTitle_, sideHelpVisible);
    showControl(sideHelpText_, sideHelpVisible);

    const int logTitleWidth = compact ? 52 : 58;
    const bool showLogTitle = mainWidth >= 520;
    showControl(logPanelTitle_, showLogTitle);
    if (showLogTitle) {
        moveControl(logPanelTitle_, mainX, logY + 2, logTitleWidth, titleHeight, layoutRepaint);
    }
    const int toolbarX = showLogTitle ? mainX + logTitleWidth + gap : mainX;
    const int toolbarWidth = std::max(1, mainWidth - (showLogTitle ? logTitleWidth + gap : 0));
    const LogToolbarLayout logLayout = calculateLogToolbarLayout(toolbarX, logY, toolbarWidth, row, gap, metrics.logActionWidth);
    showControl(logFormatLabel_, false);
    showControl(logEncodingLabel_, false);
    showControl(logFilterLabel_, false);
    showControl(logSearchLabel_, false);
    moveControl(logFormatCombo_, logLayout.formatCombo.x, logLayout.formatCombo.y, logLayout.formatCombo.width, 150, layoutRepaint);
    moveControl(logEncodingCombo_, logLayout.encodingCombo.x, logLayout.encodingCombo.y, logLayout.encodingCombo.width, 140, layoutRepaint);
    moveControl(copyLogButton_, logLayout.copyButton.x, logLayout.copyButton.y, logLayout.copyButton.width, logLayout.copyButton.height, layoutRepaint);
    moveControl(exportLogButton_, logLayout.exportButton.x, logLayout.exportButton.y, logLayout.exportButton.width, logLayout.exportButton.height, layoutRepaint);
    moveControl(logFilterEdit_, logLayout.filterEdit.x, logLayout.filterEdit.y + editOffsetY, logLayout.filterEdit.width, editHeight, layoutRepaint);
    moveControl(logSearchEdit_, logLayout.searchEdit.x, logLayout.searchEdit.y + editOffsetY, logLayout.searchEdit.width, editHeight, layoutRepaint);
    moveControl(findLogButton_, logLayout.findButton.x, logLayout.findButton.y, logLayout.findButton.width, logLayout.findButton.height, layoutRepaint);
    const int logContentY = std::max(logLayout.exportButton.bottom(), logLayout.findButton.bottom()) + (compact ? 5 : 6);
    moveControl(receiveLog_, mainX, logContentY, mainWidth, std::max(1, logY + logHeight - logContentY), layoutRepaint);

    showControl(workPanelTitle_, false);
    const int workInnerX = mainX;
    const int workInnerWidth = mainWidth;
    moveControl(workTabs_, workInnerX, tabsY, workInnerWidth, tabsHeight, layoutRepaint);

    RECT tabDisplayRect = {0, 0, workInnerWidth, tabsHeight};
    TabCtrl_AdjustRect(workTabs_, FALSE, &tabDisplayRect);
    const int pageHorizontalInset = compact ? 7 : 6;
    const int pageVerticalInset = compact ? 3 : 4;
    const int pageX = workInnerX + std::max(0L, tabDisplayRect.left) + pageHorizontalInset;
    const int pageY = tabsY + std::max(0L, tabDisplayRect.top) + pageVerticalInset;
    const int pageRight = workInnerX + std::min<LONG>(workInnerWidth, tabDisplayRect.right) - pageHorizontalInset;
    const int pageBottom = tabsY + std::min<LONG>(tabsHeight, tabDisplayRect.bottom) - pageVerticalInset;
    const int pageBackgroundX = workInnerX + std::max(0L, tabDisplayRect.left);
    const int pageBackgroundY = tabsY + std::max(0L, tabDisplayRect.top);
    const int pageBackgroundRight = workInnerX + std::min<LONG>(workInnerWidth, tabDisplayRect.right);
    const int pageBackgroundBottom = tabsY + std::min<LONG>(tabsHeight, tabDisplayRect.bottom);
    const int pageW = std::max(1, pageRight - pageX);
    const bool pageHasRoom = pageBottom > pageY;
    const int labelOffsetY = std::max(0, (row - labelHeight) / 2);
    const int checkOffsetY = std::max(0, (row - 16) / 2);
    const int progressOffsetY = compact ? 3 : 4;
    const int progressHeight = compact ? 14 : 16;
    const int formFieldGap = compact ? 2 : 3;
    const auto moveWorkEdit = [&](HWND control, int editX, int editY, int editWidth) {
        moveControl(control, editX, editY + editOffsetY, editWidth, editHeight, layoutRepaint);
    };
    moveControl(
        workPageBackground_,
        pageBackgroundX,
        pageBackgroundY,
        std::max(1, pageBackgroundRight - pageBackgroundX),
        std::max(1, pageBackgroundBottom - pageBackgroundY),
        layoutRepaint);
    const bool showSingleSendFormatRow = pageHasRoom && pageY + row <= pageBottom;

    showControl(sendModeLabel_, false);
    showControl(sendEncodingLabel_, false);
    showControl(lineEndingLabel_, false);
    showControl(sendHistoryLabel_, false);

    const int formatGapCount = pageW >= 380 ? 3 : 2;
    const int formatAvailable = std::max(3, pageW - gap * formatGapCount);
    const bool showHistoryCombo = showSingleSendFormatRow && pageW >= 380;
    const int modeWidth = std::max(1, std::min(compact ? 104 : 118, formatAvailable * 35 / 100));
    const int encodingWidth = std::max(1, std::min(compact ? 62 : 70, formatAvailable * 19 / 100));
    const int lineEndingWidth = std::max(1, std::min(compact ? 62 : 70, formatAvailable * 19 / 100));
    const int historyWidth = std::max(1, formatAvailable - modeWidth - encodingWidth - lineEndingWidth);
    x = pageX;
    y = pageY;
    moveControl(sendModeCombo_, x, y, modeWidth, 150, layoutRepaint);
    x += modeWidth + gap;
    moveControl(textEncodingCombo_, x, y, encodingWidth, 140, layoutRepaint);
    x += encodingWidth + gap;
    moveControl(lineEndingCombo_, x, y, lineEndingWidth, 140, layoutRepaint);
    x += lineEndingWidth + gap;
    moveControl(historyCombo_, x, y, historyWidth, 160, layoutRepaint);

    const int singleSendExtraGap = pageBottom - pageY >= row * 4
        ? (compact ? 4 : 8)
        : 0;
    const int singleContentY = showSingleSendFormatRow ? (pageY + row + gap + singleSendExtraGap) : pageY;

    x = pageX;
    y = singleContentY;
    const int sendButtonWidth = std::max(1, std::min(compact ? 54 : 60, pageW / 6));
    const int sendEditWidth = std::max(1, pageW - sendButtonWidth - gap);
    moveWorkEdit(sendEdit_, x, y, sendEditWidth);
    moveControl(sendButton_, x + sendEditWidth + gap, y, sendButtonWidth, row, layoutRepaint);
    const bool sendContentVisible = y + row <= pageBottom && pageW >= 96;
    const int timedX = pageX;
    const int timedY = y + row + gap;
    const int timedCheckWidth = compact ? 48 : 54;
    const int periodLabelWidth = compact ? 58 : 64;
    const int periodEditWidth = compact ? 60 : 68;
    moveControl(timedSendCheck_, timedX, timedY + checkOffsetY, timedCheckWidth, 16, layoutRepaint);
    moveControl(timedPeriodLabel_, timedX + timedCheckWidth + gap, timedY + labelOffsetY, periodLabelWidth, labelHeight, layoutRepaint);
    moveWorkEdit(timedPeriodEdit_, timedX + timedCheckWidth + gap + periodLabelWidth + formFieldGap, timedY, periodEditWidth);

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
    const int quickSlotHeight = row + gap;
    std::array<bool, 10> quickSlotVisible = {};
    for (std::size_t index = 0; index < quickSendEdits_.size(); ++index) {
        const int column = static_cast<int>(index % quickColumns);
        const int slotRow = static_cast<int>(index / quickColumns);
        const int slotX = pageX + column * (quickColumnWidth + gap);
        const int slotY = pageY + slotRow * quickSlotHeight;
        const int editWidth = std::max(1, quickColumnWidth - quickButtonWidth - gap);
        quickSlotVisible[index] = slotY + row <= pageBottom && quickColumnWidth > quickButtonWidth + gap;
        moveWorkEdit(quickSendEdits_[index], slotX, slotY, editWidth);
        moveControl(quickSendButtons_[index], slotX + editWidth + gap, slotY, quickButtonWidth, row, layoutRepaint);
    }

    x = pageX;
    y = pageY;
    const int fileLabelWidth = compact ? 30 : 34;
    const int browseWidth = compact ? 44 : 50;
    const int fileSendWidth = compact ? 68 : 76;
    const int fileStopWidth = compact ? 44 : 50;
    const bool fileFirstRowVisible = y + row <= pageBottom && pageW >= fileLabelWidth + browseWidth + fileSendWidth + fileStopWidth + gap * 4 + 24;
    const int filePathWidth = std::max(1, pageW - fileLabelWidth - browseWidth - fileSendWidth - fileStopWidth - gap * 4 - formFieldGap);
    moveControl(filePathLabel_, x, y + labelOffsetY, fileLabelWidth, labelHeight, layoutRepaint);
    x += fileLabelWidth + formFieldGap;
    moveWorkEdit(filePathEdit_, x, y, filePathWidth);
    x += filePathWidth + gap;
    moveControl(fileBrowseButton_, x, y, browseWidth, row, layoutRepaint);
    x += browseWidth + gap;
    moveControl(fileSendButton_, x, y, fileSendWidth, row, layoutRepaint);
    x += fileSendWidth + gap;
    moveControl(fileStopButton_, x, y, fileStopWidth, row, layoutRepaint);
    y += row + gap;
    x = pageX;
    const int delayLabelWidth = compact ? 60 : 68;
    const int delayComboWidth = compact ? 66 : 74;
    const int fileProgressLabelWidth = compact ? 60 : 68;
    const bool fileSecondRowVisible = y + row <= pageBottom;
    moveControl(fileDelayLabel_, x, y + labelOffsetY, delayLabelWidth, labelHeight, layoutRepaint);
    x += delayLabelWidth + formFieldGap;
    moveControl(fileDelayCombo_, x, y, delayComboWidth, 160, layoutRepaint);
    x += delayComboWidth + gap;
    moveControl(fileProgressLabel_, x, y + labelOffsetY, fileProgressLabelWidth, labelHeight, layoutRepaint);
    x += fileProgressLabelWidth + formFieldGap;
    moveTopControl(fileProgress_, x, y + progressOffsetY, std::max(1, pageX + pageW - x), progressHeight, layoutRepaint);

    ShowWindow(workflowHint_, SW_HIDE);
    y = pageY;
    const int scanSectionGap = compact ? 2 : 3;
    const int scanBlockGap = compact ? 3 : 4;
    const bool scanSectionVisible = y + labelHeight <= pageBottom;
    moveControl(scanSectionLabel_, pageX, y, pageW, labelHeight, layoutRepaint);
    y += labelHeight + scanSectionGap;
    x = pageX;
    const int shortLabelWidth = compact ? 28 : 32;
    const int scanSlaveEditWidth = compact ? 34 : 40;
    const int addressLabelWidth = compact ? 52 : 58;
    const int addressEditWidth = compact ? 46 : 52;
    const int scanFunctionLabelWidth = compact ? 32 : 36;
    const int modbusButtonWidth = compact ? 94 : 102;
    const int fixedScanRowWidth =
        shortLabelWidth + scanSlaveEditWidth
        + scanFunctionLabelWidth
        + addressLabelWidth + addressEditWidth
        + addressLabelWidth + addressEditWidth
        + modbusButtonWidth
        + gap * 4
        + formFieldGap * 4;
    const int scanFunctionDesiredWidth = compact ? 144 : 168;
    const int scanFunctionWidth = std::max(1, std::min(scanFunctionDesiredWidth, pageW - fixedScanRowWidth));
    const bool scanParameterRowVisible = y + row <= pageBottom && pageW >= fixedScanRowWidth + 80;
    moveControl(scanSlaveLabel_, x, y + labelOffsetY, shortLabelWidth, labelHeight, layoutRepaint);
    x += shortLabelWidth + formFieldGap;
    moveWorkEdit(scanSlaveEdit_, x, y, scanSlaveEditWidth);
    x += scanSlaveEditWidth + gap;
    moveControl(scanFunctionLabel_, x, y + labelOffsetY, scanFunctionLabelWidth, labelHeight, layoutRepaint);
    x += scanFunctionLabelWidth + formFieldGap;
    moveControl(scanFunctionCombo_, x, y, scanFunctionWidth, 160, layoutRepaint);
    x += scanFunctionWidth + gap;
    moveControl(scanStartLabel_, x, y + labelOffsetY, addressLabelWidth, labelHeight, layoutRepaint);
    x += addressLabelWidth + formFieldGap;
    moveWorkEdit(scanStartEdit_, x, y, addressEditWidth);
    x += addressEditWidth + gap;
    moveControl(scanEndLabel_, x, y + labelOffsetY, addressLabelWidth, labelHeight, layoutRepaint);
    x += addressLabelWidth + formFieldGap;
    moveWorkEdit(scanEndEdit_, x, y, addressEditWidth);
    x += addressEditWidth + gap;
    moveControl(modbusButton_, x, y, modbusButtonWidth, row, layoutRepaint);

    y += row + scanBlockGap;
    x = pageX;
    const int progressLabelWidth = compact ? 32 : 36;
    const int progressTextWidth = std::max(1, std::min(compact ? 126 : 148, pageW / 3));
    const int progressBarWidth = std::max(1, pageW - progressLabelWidth - progressTextWidth - gap * 2);
    const bool scanProgressRowVisible = y + row <= pageBottom && pageW >= 180;
    moveControl(modbusProgressLabel_, x, y + labelOffsetY, progressLabelWidth, labelHeight, layoutRepaint);
    x += progressLabelWidth + gap;
    moveTopControl(modbusProgress_, x, y + progressOffsetY, progressBarWidth, progressHeight, layoutRepaint);
    x += progressBarWidth + gap;
    moveControl(modbusProgressText_, x, y + labelOffsetY, progressTextWidth, labelHeight, layoutRepaint);

    y += row + scanBlockGap;
    x = pageX;
    const bool scanAnalysisSectionVisible = y + labelHeight <= pageBottom;
    moveControl(analysisSectionLabel_, pageX, y, pageW, labelHeight, layoutRepaint);
    y += labelHeight + scanSectionGap;
    const bool scanTargetRowVisible = y + row <= pageBottom;
    const int targetNameLabelWidth = compact ? 42 : 48;
    const int targetValueLabelWidth = compact ? 42 : 48;
    const int targetUnitLabelWidth = compact ? 28 : 32;
    const int toleranceLabelWidth = compact ? 28 : 32;
    const int targetNameDesiredWidth = compact ? 100 : 126;
    const int targetValueDesiredWidth = compact ? 84 : 96;
    const int targetUnitDesiredWidth = compact ? 52 : 64;
    const int toleranceDesiredWidth = compact ? 58 : 68;
    const int targetDesiredEditWidth =
        targetNameDesiredWidth + targetValueDesiredWidth + targetUnitDesiredWidth + toleranceDesiredWidth;
    const int targetAvailableEditWidth = std::max(4, pageW
        - targetNameLabelWidth
        - targetValueLabelWidth
        - targetUnitLabelWidth
        - toleranceLabelWidth
        - gap * 3
        - formFieldGap * 4);
    const bool useStandardTargetEditWidths = targetAvailableEditWidth >= targetDesiredEditWidth;
    const int targetNameEditWidth = useStandardTargetEditWidths
        ? targetNameDesiredWidth
        : std::max(1, targetNameDesiredWidth * targetAvailableEditWidth / targetDesiredEditWidth);
    const int targetValueEditWidth = useStandardTargetEditWidths
        ? targetValueDesiredWidth
        : std::max(1, targetValueDesiredWidth * targetAvailableEditWidth / targetDesiredEditWidth);
    const int targetUnitEditWidth = useStandardTargetEditWidths
        ? targetUnitDesiredWidth
        : std::max(1, targetUnitDesiredWidth * targetAvailableEditWidth / targetDesiredEditWidth);
    const int toleranceEditWidth = useStandardTargetEditWidths
        ? toleranceDesiredWidth
        : std::max(1, targetAvailableEditWidth - targetNameEditWidth - targetValueEditWidth - targetUnitEditWidth);
    moveControl(targetStatic_, x, y + labelOffsetY, targetNameLabelWidth, labelHeight, layoutRepaint);
    x += targetNameLabelWidth + formFieldGap;
    moveWorkEdit(targetLabelEdit_, x, y, targetNameEditWidth);
    x += targetNameEditWidth + gap;
    moveControl(targetValueStatic_, x, y + labelOffsetY, targetValueLabelWidth, labelHeight, layoutRepaint);
    x += targetValueLabelWidth + formFieldGap;
    moveWorkEdit(targetValueEdit_, x, y, targetValueEditWidth);
    x += targetValueEditWidth + gap;
    moveControl(targetUnitStatic_, x, y + labelOffsetY, targetUnitLabelWidth, labelHeight, layoutRepaint);
    x += targetUnitLabelWidth + formFieldGap;
    moveWorkEdit(targetUnitEdit_, x, y, targetUnitEditWidth);
    x += targetUnitEditWidth + gap;
    moveControl(toleranceStatic_, x, y + labelOffsetY, toleranceLabelWidth, labelHeight, layoutRepaint);
    x += toleranceLabelWidth + formFieldGap;
    moveWorkEdit(toleranceEdit_, x, y, toleranceEditWidth);

    y += row + scanBlockGap;
    x = pageX;
    const bool scanCandidateRowVisible = y + row <= pageBottom;
    const int candidateLabelWidth = compact ? 32 : 36;
    const int analysisButtonWidth = compact ? 66 : 76;
    const int ruleButtonWidth = compact ? 66 : 76;
    const int exportButtonWidth = compact ? 66 : 76;
    const int candidateWidth = std::max(1, pageW
        - candidateLabelWidth
        - analysisButtonWidth
        - ruleButtonWidth
        - exportButtonWidth
        - gap * 3
        - formFieldGap);
    moveControl(candidateStatic_, x, y + labelOffsetY, candidateLabelWidth, labelHeight, layoutRepaint);
    x += candidateLabelWidth + formFieldGap;
    moveControl(candidateCombo_, x, y, candidateWidth, 180, layoutRepaint);
    x += candidateWidth + gap;
    moveControl(analysisButton_, x, y, analysisButtonWidth, row, layoutRepaint);
    x += analysisButtonWidth + gap;
    moveControl(ruleVerifyButton_, x, y, ruleButtonWidth, row, layoutRepaint);
    x += ruleButtonWidth + gap;
    moveControl(exportReportButton_, x, y, exportButtonWidth, row, layoutRepaint);

    y = pageY;
    x = pageX;
    const int settingsLabelWidth = compact ? 58 : 66;
    const int settingsComboWidth = compact ? 68 : 78;
    const int rawRetentionLabelWidth = compact ? 58 : 66;
    const int rawRetentionComboWidth = compact ? 78 : 90;
    const bool settingsRowVisible = y + row <= pageBottom;
    moveControl(logCacheLabel_, x, y + labelOffsetY, settingsLabelWidth, labelHeight, layoutRepaint);
    x += settingsLabelWidth + formFieldGap;
    moveControl(logCacheCombo_, x, y, settingsComboWidth, 260, layoutRepaint);
    x += settingsComboWidth + gap;
    moveControl(rawEventRetentionLabel_, x, y + labelOffsetY, rawRetentionLabelWidth, labelHeight, layoutRepaint);
    x += rawRetentionLabelWidth + formFieldGap;
    moveControl(rawEventRetentionCombo_, x, y, rawRetentionComboWidth, 140, layoutRepaint);

    const bool pageVisible = pageBottom > pageY;
    showControl(workTabs_, pageVisible);
    showControl(workPageBackground_, pageVisible);
    workbenchVisibility_ = {};
    workbenchVisibility_.pageVisible = pageVisible;
    workbenchVisibility_.singleFormatRow = showSingleSendFormatRow;
    workbenchVisibility_.singleHistory = showHistoryCombo;
    workbenchVisibility_.singleSend = sendContentVisible;
    workbenchVisibility_.singleTimed = timedY + row <= pageBottom && pageW >= 230;
    workbenchVisibility_.quickSlots = quickSlotVisible;
    workbenchVisibility_.fileFirstRow = fileFirstRowVisible;
    workbenchVisibility_.fileSecondRow = fileSecondRowVisible;
    workbenchVisibility_.scanSection = scanSectionVisible;
    workbenchVisibility_.scanParameterRow = scanParameterRowVisible;
    workbenchVisibility_.scanProgressRow = scanProgressRowVisible;
    workbenchVisibility_.scanAnalysisSection = scanAnalysisSectionVisible;
    workbenchVisibility_.scanTargetRow = scanTargetRowVisible;
    workbenchVisibility_.scanCandidateRow = scanCandidateRowVisible;
    workbenchVisibility_.settingsRow = settingsRowVisible;
    workbenchRedrawRect_ = {workInnerX, tabsY, workInnerX + workInnerWidth, tabsY + tabsHeight};
    workbenchVisibilityReady_ = true;
    workbenchTabState_.noteLayoutChanged();
    int activeTab = static_cast<int>(TabCtrl_GetCurSel(workTabs_));
    if (activeTab < 0) {
        activeTab = 0;
    }
    applyWorkbenchTabVisibility(activeTab);
    const int clockWidth = compact ? 78 : 88;
    const int counterWidth = compact ? 76 : 86;
    int statusRight = width - margin;
    const bool showClock = width >= 560;
    const bool showCounters = width >= 470;
    showControl(clockStatusText_, showClock);
    showControl(rxStatusText_, showCounters);
    showControl(txStatusText_, showCounters);
    if (showClock) {
        moveControl(clockStatusText_, statusRight - clockWidth, statusY, clockWidth, statusHeight, layoutRepaint);
        statusRight -= clockWidth + gap;
    }
    if (showCounters) {
        moveControl(rxStatusText_, statusRight - counterWidth, statusY, counterWidth, statusHeight, layoutRepaint);
        statusRight -= counterWidth + gap;
        moveControl(txStatusText_, statusRight - counterWidth, statusY, counterWidth, statusHeight, layoutRepaint);
        statusRight -= counterWidth + gap;
    }
    moveControl(statusText_, margin, statusY, std::max(1, statusRight - margin), statusHeight, layoutRepaint);
}

} // namespace svm::win32

#endif
