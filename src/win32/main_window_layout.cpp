#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_layout_metrics.h"

#include <algorithm>
#include <array>
#include <commctrl.h>

namespace svm::win32 {

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

    const int desiredWorkHeight = metrics.desiredWorkHeight;
    const int minimumLogHeight = metrics.minimumLogHeight;
    const int maximumWorkHeight = std::max(84, contentHeight - minimumLogHeight - sideGap);
    const int workHeight = std::max(84, std::min(desiredWorkHeight, maximumWorkHeight));
    const int logY = margin;
    const int logHeight = std::max(1, statusY - logY - workHeight - sideGap);
    const int sendY = logY + logHeight + sideGap;
    const int sendHeight = std::max(1, statusY - sendY - 2);
    const int tabsY = sendY;
    const int tabsHeight = std::max(84, sendHeight);

    showControl(connectionGroup_, false);
    showControl(sendGroup_, false);
    showControl(workflowGroup_, false);
    showControl(logGroup_, false);

    MoveWindow(serialPanelTitle_, sideX, margin, sideWidth, titleHeight, TRUE);
    int x = sideX;
    int y = margin + titleHeight + gap;
    const int sideInnerWidth = std::max(1, sideWidth);
    const int sideLabelWidth = compact ? 44 : 48;
    const int sideControlWidth = std::max(1, sideInnerWidth - sideLabelWidth - gap);

    showControl(portLabel_, false);
    MoveWindow(portCombo_, x, y, sideInnerWidth, 180, TRUE);
    y += row + gap;
    MoveWindow(refreshButton_, x, y, (sideInnerWidth - gap) / 2, row, TRUE);
    MoveWindow(saveProfileButton_, x + (sideInnerWidth + gap) / 2, y, (sideInnerWidth - gap) / 2, row, TRUE);
    y += row + gap;

    const auto moveSidePair = [&](HWND label, HWND control, int comboDropHeight) {
        MoveWindow(label, x, y + 4, sideLabelWidth, labelHeight, TRUE);
        MoveWindow(control, x + sideLabelWidth + gap, y, sideControlWidth, comboDropHeight, TRUE);
        y += row + gap;
    };
    moveSidePair(baudLabel_, baudCombo_, 180);
    moveSidePair(stopBitsLabel_, stopBitsCombo_, 140);
    moveSidePair(dataBitsLabel_, dataBitsCombo_, 140);
    moveSidePair(parityLabel_, parityCombo_, 150);
    moveSidePair(flowControlLabel_, flowControlCombo_, 150);

    MoveWindow(connectButton_, x, y, sideInnerWidth, row, TRUE);
    showControl(disconnectButton_, false);
    y += row + gap;
    MoveWindow(dtrCheck_, x, y + 2, sideInnerWidth / 2, row - 2, TRUE);
    MoveWindow(rtsCheck_, x + sideInnerWidth / 2, y + 2, sideInnerWidth / 2, row - 2, TRUE);
    y += row;
    MoveWindow(autoReconnectCheck_, x, y + 2, sideInnerWidth, row - 2, TRUE);
    const int sideControlBottom = y + row;
    const int sideSeparatorHeight = 2;
    const int sideActionGap = compact ? 2 : 4;
    const int sideHelpMinimumHeight = compact ? 86 : 100;
    const int sideActionSeparatorY = sideControlBottom + sideActionGap;
    const int sideActionButtonY = sideActionSeparatorY + sideSeparatorHeight + sideActionGap;
    const int sideHelpSeparatorY = sideActionButtonY + row + sideActionGap;
    const int sideHelpY = std::max(tabsY, sideHelpSeparatorY + sideSeparatorHeight + sideActionGap);
    const int sideHelpHeight = std::min(tabsHeight, std::max(1, statusY - sideHelpY - 2));
    const bool sideActionVisible = sideInnerWidth >= 108
        && sideActionButtonY + row <= statusY - margin;
    MoveWindow(sideActionSeparator_, x, sideActionSeparatorY, sideInnerWidth, sideSeparatorHeight, TRUE);
    showControl(sideActionSeparator_, sideActionVisible);
    const int sideActionButtonWidth = std::max(1, (sideInnerWidth - gap) / 2);
    MoveWindow(pauseScrollButton_, x, sideActionButtonY, sideActionButtonWidth, row, TRUE);
    MoveWindow(clearButton_, x + sideActionButtonWidth + gap, sideActionButtonY, sideActionButtonWidth, row, TRUE);
    showControl(pauseScrollButton_, sideActionVisible);
    showControl(clearButton_, sideActionVisible);
    const bool sideHelpVisible = sideActionVisible
        && sideHelpHeight >= sideHelpMinimumHeight
        && sideHelpY + sideHelpHeight <= statusY;
    MoveWindow(sideHelpSeparator_, x, sideHelpSeparatorY, sideInnerWidth, sideSeparatorHeight, TRUE);
    showControl(sideHelpSeparator_, sideHelpVisible);
    if (sideHelpVisible) {
        const int helpHeight = sideHelpHeight;
        const int helpY = sideHelpY;
        const int helpPad = compact ? 6 : 7;
        const int helpTitleHeight = compact ? 17 : 18;
        MoveWindow(sideHelpFrame_, x, helpY, sideInnerWidth, helpHeight, TRUE);
        MoveWindow(sideHelpTitle_, x + helpPad, helpY + helpPad, std::max(1, sideInnerWidth - helpPad * 2), helpTitleHeight, TRUE);
        MoveWindow(
            sideHelpText_,
            x + helpPad,
            helpY + helpPad + helpTitleHeight + (compact ? 3 : 4),
            std::max(1, sideInnerWidth - helpPad * 2),
            std::max(1, helpHeight - helpPad * 2 - helpTitleHeight - (compact ? 3 : 4)),
            TRUE);
    } else {
        MoveWindow(sideHelpFrame_, x, sideHelpY, 1, 1, TRUE);
        MoveWindow(sideHelpTitle_, x, sideHelpY, 1, 1, TRUE);
        MoveWindow(sideHelpText_, x, sideHelpY, 1, 1, TRUE);
    }
    showControl(sideHelpFrame_, sideHelpVisible);
    showControl(sideHelpTitle_, sideHelpVisible);
    showControl(sideHelpText_, sideHelpVisible);

    const int logTitleWidth = compact ? 52 : 58;
    const bool showLogTitle = mainWidth >= 520;
    showControl(logPanelTitle_, showLogTitle);
    if (showLogTitle) {
        MoveWindow(logPanelTitle_, mainX, logY + 2, logTitleWidth, titleHeight, TRUE);
    }
    const int toolbarX = showLogTitle ? mainX + logTitleWidth + gap : mainX;
    const int toolbarWidth = std::max(1, mainWidth - (showLogTitle ? logTitleWidth + gap : 0));
    const LogToolbarLayout logLayout = calculateLogToolbarLayout(toolbarX, logY, toolbarWidth, row, gap, metrics.logActionWidth);
    showControl(logFormatLabel_, false);
    showControl(logEncodingLabel_, false);
    showControl(logFilterLabel_, false);
    showControl(logSearchLabel_, false);
    MoveWindow(logFormatCombo_, logLayout.formatCombo.x, logLayout.formatCombo.y, logLayout.formatCombo.width, 150, TRUE);
    MoveWindow(logEncodingCombo_, logLayout.encodingCombo.x, logLayout.encodingCombo.y, logLayout.encodingCombo.width, 140, TRUE);
    MoveWindow(copyLogButton_, logLayout.copyButton.x, logLayout.copyButton.y, logLayout.copyButton.width, logLayout.copyButton.height, TRUE);
    MoveWindow(exportLogButton_, logLayout.exportButton.x, logLayout.exportButton.y, logLayout.exportButton.width, logLayout.exportButton.height, TRUE);
    MoveWindow(logFilterEdit_, logLayout.filterEdit.x, logLayout.filterEdit.y + editOffsetY, logLayout.filterEdit.width, editHeight, TRUE);
    MoveWindow(logSearchEdit_, logLayout.searchEdit.x, logLayout.searchEdit.y + editOffsetY, logLayout.searchEdit.width, editHeight, TRUE);
    MoveWindow(findLogButton_, logLayout.findButton.x, logLayout.findButton.y, logLayout.findButton.width, logLayout.findButton.height, TRUE);
    const int logContentY = std::max(logLayout.exportButton.bottom(), logLayout.findButton.bottom()) + (compact ? 5 : 6);
    MoveWindow(receiveLog_, mainX, logContentY, mainWidth, std::max(1, logY + logHeight - logContentY), TRUE);

    showControl(workPanelTitle_, false);
    const int workInnerX = mainX;
    const int workInnerWidth = mainWidth;
    MoveWindow(workTabs_, workInnerX, tabsY, workInnerWidth, tabsHeight, TRUE);

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
        MoveWindow(control, editX, editY + editOffsetY, editWidth, editHeight, TRUE);
    };
    MoveWindow(
        workPageBackground_,
        pageBackgroundX,
        pageBackgroundY,
        std::max(1, pageBackgroundRight - pageBackgroundX),
        std::max(1, pageBackgroundBottom - pageBackgroundY),
        TRUE);
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
    MoveWindow(sendModeCombo_, x, y, modeWidth, 150, TRUE);
    x += modeWidth + gap;
    MoveWindow(textEncodingCombo_, x, y, encodingWidth, 140, TRUE);
    x += encodingWidth + gap;
    MoveWindow(lineEndingCombo_, x, y, lineEndingWidth, 140, TRUE);
    x += lineEndingWidth + gap;
    MoveWindow(historyCombo_, x, y, historyWidth, 160, TRUE);

    const int singleContentY = showSingleSendFormatRow ? (pageY + row + gap) : pageY;

    x = pageX;
    y = singleContentY;
    const int sendButtonWidth = std::max(1, std::min(compact ? 54 : 60, pageW / 6));
    const int sendEditWidth = std::max(1, pageW - sendButtonWidth - gap);
    moveWorkEdit(sendEdit_, x, y, sendEditWidth);
    MoveWindow(sendButton_, x + sendEditWidth + gap, y, sendButtonWidth, row, TRUE);
    const bool sendContentVisible = y + row <= pageBottom && pageW >= 96;
    const int timedX = pageX;
    const int timedY = y + row + gap;
    const int timedCheckWidth = compact ? 48 : 54;
    const int periodLabelWidth = compact ? 58 : 64;
    const int periodEditWidth = compact ? 60 : 68;
    MoveWindow(timedSendCheck_, timedX, timedY + checkOffsetY, timedCheckWidth, 16, TRUE);
    MoveWindow(timedPeriodLabel_, timedX + timedCheckWidth + gap, timedY + labelOffsetY, periodLabelWidth, labelHeight, TRUE);
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
        MoveWindow(quickSendButtons_[index], slotX + editWidth + gap, slotY, quickButtonWidth, row, TRUE);
    }

    x = pageX;
    y = pageY;
    const int fileLabelWidth = compact ? 30 : 34;
    const int browseWidth = compact ? 44 : 50;
    const int fileSendWidth = compact ? 68 : 76;
    const int fileStopWidth = compact ? 44 : 50;
    const bool fileFirstRowVisible = y + row <= pageBottom && pageW >= fileLabelWidth + browseWidth + fileSendWidth + fileStopWidth + gap * 4 + 24;
    const int filePathWidth = std::max(1, pageW - fileLabelWidth - browseWidth - fileSendWidth - fileStopWidth - gap * 4 - formFieldGap);
    MoveWindow(filePathLabel_, x, y + labelOffsetY, fileLabelWidth, labelHeight, TRUE);
    x += fileLabelWidth + formFieldGap;
    moveWorkEdit(filePathEdit_, x, y, filePathWidth);
    x += filePathWidth + gap;
    MoveWindow(fileBrowseButton_, x, y, browseWidth, row, TRUE);
    x += browseWidth + gap;
    MoveWindow(fileSendButton_, x, y, fileSendWidth, row, TRUE);
    x += fileSendWidth + gap;
    MoveWindow(fileStopButton_, x, y, fileStopWidth, row, TRUE);
    y += row + gap;
    x = pageX;
    const int delayLabelWidth = compact ? 60 : 68;
    const int delayComboWidth = compact ? 66 : 74;
    const int fileProgressLabelWidth = compact ? 60 : 68;
    const bool fileSecondRowVisible = y + row <= pageBottom;
    MoveWindow(fileDelayLabel_, x, y + labelOffsetY, delayLabelWidth, labelHeight, TRUE);
    x += delayLabelWidth + formFieldGap;
    MoveWindow(fileDelayCombo_, x, y, delayComboWidth, 160, TRUE);
    x += delayComboWidth + gap;
    MoveWindow(fileProgressLabel_, x, y + labelOffsetY, fileProgressLabelWidth, labelHeight, TRUE);
    x += fileProgressLabelWidth + formFieldGap;
    moveTopControl(fileProgress_, x, y + progressOffsetY, std::max(1, pageX + pageW - x), progressHeight);

    ShowWindow(workflowHint_, SW_HIDE);
    y = pageY;
    const int scanSectionGap = compact ? 2 : 3;
    const int scanBlockGap = compact ? 3 : 4;
    const bool scanSectionVisible = y + labelHeight <= pageBottom;
    MoveWindow(scanSectionLabel_, pageX, y, pageW, labelHeight, TRUE);
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
    MoveWindow(scanSlaveLabel_, x, y + labelOffsetY, shortLabelWidth, labelHeight, TRUE);
    x += shortLabelWidth + formFieldGap;
    moveWorkEdit(scanSlaveEdit_, x, y, scanSlaveEditWidth);
    x += scanSlaveEditWidth + gap;
    MoveWindow(scanFunctionLabel_, x, y + labelOffsetY, scanFunctionLabelWidth, labelHeight, TRUE);
    x += scanFunctionLabelWidth + formFieldGap;
    MoveWindow(scanFunctionCombo_, x, y, scanFunctionWidth, 160, TRUE);
    x += scanFunctionWidth + gap;
    MoveWindow(scanStartLabel_, x, y + labelOffsetY, addressLabelWidth, labelHeight, TRUE);
    x += addressLabelWidth + formFieldGap;
    moveWorkEdit(scanStartEdit_, x, y, addressEditWidth);
    x += addressEditWidth + gap;
    MoveWindow(scanEndLabel_, x, y + labelOffsetY, addressLabelWidth, labelHeight, TRUE);
    x += addressLabelWidth + formFieldGap;
    moveWorkEdit(scanEndEdit_, x, y, addressEditWidth);
    x += addressEditWidth + gap;
    MoveWindow(modbusButton_, x, y, modbusButtonWidth, row, TRUE);

    y += row + scanBlockGap;
    x = pageX;
    const int progressLabelWidth = compact ? 32 : 36;
    const int progressTextWidth = std::max(1, std::min(compact ? 126 : 148, pageW / 3));
    const int progressBarWidth = std::max(1, pageW - progressLabelWidth - progressTextWidth - gap * 2);
    const bool scanProgressRowVisible = y + row <= pageBottom && pageW >= 180;
    MoveWindow(modbusProgressLabel_, x, y + labelOffsetY, progressLabelWidth, labelHeight, TRUE);
    x += progressLabelWidth + gap;
    moveTopControl(modbusProgress_, x, y + progressOffsetY, progressBarWidth, progressHeight);
    x += progressBarWidth + gap;
    MoveWindow(modbusProgressText_, x, y + labelOffsetY, progressTextWidth, labelHeight, TRUE);

    y += row + scanBlockGap;
    x = pageX;
    const bool scanAnalysisSectionVisible = y + labelHeight <= pageBottom;
    MoveWindow(analysisSectionLabel_, pageX, y, pageW, labelHeight, TRUE);
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
    MoveWindow(targetStatic_, x, y + labelOffsetY, targetNameLabelWidth, labelHeight, TRUE);
    x += targetNameLabelWidth + formFieldGap;
    moveWorkEdit(targetLabelEdit_, x, y, targetNameEditWidth);
    x += targetNameEditWidth + gap;
    MoveWindow(targetValueStatic_, x, y + labelOffsetY, targetValueLabelWidth, labelHeight, TRUE);
    x += targetValueLabelWidth + formFieldGap;
    moveWorkEdit(targetValueEdit_, x, y, targetValueEditWidth);
    x += targetValueEditWidth + gap;
    MoveWindow(targetUnitStatic_, x, y + labelOffsetY, targetUnitLabelWidth, labelHeight, TRUE);
    x += targetUnitLabelWidth + formFieldGap;
    moveWorkEdit(targetUnitEdit_, x, y, targetUnitEditWidth);
    x += targetUnitEditWidth + gap;
    MoveWindow(toleranceStatic_, x, y + labelOffsetY, toleranceLabelWidth, labelHeight, TRUE);
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
    MoveWindow(candidateStatic_, x, y + labelOffsetY, candidateLabelWidth, labelHeight, TRUE);
    x += candidateLabelWidth + formFieldGap;
    MoveWindow(candidateCombo_, x, y, candidateWidth, 180, TRUE);
    x += candidateWidth + gap;
    MoveWindow(analysisButton_, x, y, analysisButtonWidth, row, TRUE);
    x += analysisButtonWidth + gap;
    MoveWindow(ruleVerifyButton_, x, y, ruleButtonWidth, row, TRUE);
    x += ruleButtonWidth + gap;
    MoveWindow(exportReportButton_, x, y, exportButtonWidth, row, TRUE);

    y = pageY;
    x = pageX;
    const int settingsLabelWidth = compact ? 58 : 66;
    const int settingsComboWidth = compact ? 68 : 78;
    const int rawRetentionLabelWidth = compact ? 58 : 66;
    const int rawRetentionComboWidth = compact ? 78 : 90;
    const bool settingsRowVisible = y + row <= pageBottom;
    MoveWindow(logCacheLabel_, x, y + labelOffsetY, settingsLabelWidth, labelHeight, TRUE);
    x += settingsLabelWidth + formFieldGap;
    MoveWindow(logCacheCombo_, x, y, settingsComboWidth, 260, TRUE);
    x += settingsComboWidth + gap;
    MoveWindow(rawEventRetentionLabel_, x, y + labelOffsetY, rawRetentionLabelWidth, labelHeight, TRUE);
    x += rawRetentionLabelWidth + formFieldGap;
    MoveWindow(rawEventRetentionCombo_, x, y, rawRetentionComboWidth, 140, TRUE);

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
        MoveWindow(clockStatusText_, statusRight - clockWidth, statusY, clockWidth, statusHeight, TRUE);
        statusRight -= clockWidth + gap;
    }
    if (showCounters) {
        MoveWindow(rxStatusText_, statusRight - counterWidth, statusY, counterWidth, statusHeight, TRUE);
        statusRight -= counterWidth + gap;
        MoveWindow(txStatusText_, statusRight - counterWidth, statusY, counterWidth, statusHeight, TRUE);
        statusRight -= counterWidth + gap;
    }
    MoveWindow(statusText_, margin, statusY, std::max(1, statusRight - margin), statusHeight, TRUE);
}

} // namespace svm::win32

#endif
