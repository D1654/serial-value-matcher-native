#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_layout_model.h"
#include "win32/native_layout_transaction.h"
#include "win32/native_layout_metrics.h"
#include "win32/native_paint_policy.h"
#include "win32/native_ui_preferences.h"

#include <algorithm>
#include <commctrl.h>

namespace svm::win32 {
namespace {

bool rectIsValid(const RECT& rect) noexcept {
    return rect.right > rect.left && rect.bottom > rect.top;
}

void includeRect(RECT& target, const RECT& rect) noexcept {
    if (!rectIsValid(rect)) {
        return;
    }
    if (!rectIsValid(target)) {
        target = rect;
        return;
    }
    target.left = std::min(target.left, rect.left);
    target.top = std::min(target.top, rect.top);
    target.right = std::max(target.right, rect.right);
    target.bottom = std::max(target.bottom, rect.bottom);
}

void includeControlRect(HWND parent, HWND control, RECT& target) {
    if (parent == nullptr || control == nullptr) {
        return;
    }
    RECT rect = {};
    if (GetWindowRect(control, &rect) == FALSE) {
        return;
    }
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
    includeRect(target, rect);
}

RECT toWinRect(const NativeRect& rect) noexcept {
    return {rect.x, rect.y, rect.right(), rect.bottom()};
}

} // namespace

bool NativeMainWindow::splitterHitTest(int x, int y) const noexcept {
    return workbenchSplitterRect_.right > workbenchSplitterRect_.left
        && workbenchSplitterRect_.bottom > workbenchSplitterRect_.top
        && x >= workbenchSplitterRect_.left
        && x < workbenchSplitterRect_.right
        && y >= workbenchSplitterRect_.top
        && y < workbenchSplitterRect_.bottom;
}

int NativeMainWindow::clampedWorkbenchHeightForClient(int requestedHeight, int width, int height) const {
    int activeTab = static_cast<int>(TabCtrl_GetCurSel(workTabs_));
    if (activeTab < 0) {
        activeTab = 0;
    }
    const NativeMainLayoutModel layoutModel = calculateNativeMainLayoutModel({
        std::max(width, 1),
        std::max(height, 1),
        nativeNormalizeWorkbenchHeight(requestedHeight),
        activeTab,
    });
    return layoutModel.workbench.workbenchHeight;
}

void NativeMainWindow::relayoutCurrentClient(bool immediate) {
    if (window_ == nullptr) {
        return;
    }
    RECT clientRect = {};
    GetClientRect(window_, &clientRect);
    RECT dragRedrawRect = {};
    if (!immediate) {
        includeControlRect(window_, receiveLog_, dragRedrawRect);
        includeControlRect(window_, workTabs_, dragRedrawRect);
        includeControlRect(window_, workPageBackground_, dragRedrawRect);
        includeRect(dragRedrawRect, workbenchRedrawRect_);
        includeRect(dragRedrawRect, workbenchSplitterRect_);
        nativeSetWindowRedraw(window_, false);
    }
    layoutControls(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
    if (!immediate) {
        includeControlRect(window_, receiveLog_, dragRedrawRect);
        includeControlRect(window_, workTabs_, dragRedrawRect);
        includeControlRect(window_, workPageBackground_, dragRedrawRect);
        includeRect(dragRedrawRect, workbenchRedrawRect_);
        includeRect(dragRedrawRect, workbenchSplitterRect_);
        nativeSetWindowRedraw(window_, true);
        if (rectIsValid(dragRedrawRect)) {
            nativeRedrawLiveRegion(window_, &dragRedrawRect);
        }
        return;
    }
    nativeRedrawFullRefresh(window_);
}

void NativeMainWindow::scheduleResizeFrame(int width, int height) {
    postNativeFrameMessage(frameScheduler_.requestResize(width, height));
}

void NativeMainWindow::scheduleSplitterDragFrame(int width, int height, int workbenchHeight) {
    postNativeFrameMessage(frameScheduler_.requestSplitterDrag(width, height, workbenchHeight));
}

void NativeMainWindow::scheduleWorkbenchTabFrame() {
    postNativeFrameMessage(frameScheduler_.requestTabSwitch());
}

void NativeMainWindow::scheduleLogFlushFrame() {
    postNativeFrameMessage(frameScheduler_.requestLogFlush());
}

void NativeMainWindow::scheduleStatusFrame() {
    postNativeFrameMessage(frameScheduler_.requestStatus());
}

void NativeMainWindow::postNativeFrameMessage(bool shouldPost) {
    if (!shouldPost) {
        return;
    }
    if (window_ == nullptr || PostMessageW(window_, kNativeUiFrameMessage, 0, 0) == FALSE) {
        frameScheduler_.markPostFailed();
        processNativeFrame();
    }
}

void NativeMainWindow::processNativeFrame() {
    const NativeFrameSnapshot frame = frameScheduler_.consumeFrame();
    if (frame.empty()) {
        return;
    }

    if (frame.hasWorkbenchHeight) {
        preferredWorkbenchHeight_ = frame.workbenchHeight;
    }

    if (nativeFrameHasReason(frame.reasons, NativeFrameReason::SplitterDrag)) {
        relayoutCurrentClient(false);
    } else if (nativeFrameHasReason(frame.reasons, NativeFrameReason::Resize) && frame.hasClientSize) {
        layoutControls(frame.clientWidth, frame.clientHeight);
        nativeRedrawFullRefresh(window_);
    }

    if (nativeFrameHasReason(frame.reasons, NativeFrameReason::TabSwitch) && workbenchTabRepaintPending_) {
        repaintWorkbenchTabControls();
    }
    if (nativeFrameHasReason(frame.reasons, NativeFrameReason::LogFlush)) {
        flushPendingLogEntries();
    }
    if (nativeFrameHasReason(frame.reasons, NativeFrameReason::Status)) {
        updateStatusSegments();
    }
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

    int activeTab = static_cast<int>(TabCtrl_GetCurSel(workTabs_));
    if (activeTab < 0) {
        activeTab = 0;
    }

    const NativeMainLayoutModel layoutModel = calculateNativeMainLayoutModel({
        std::max(width, 1),
        std::max(height, 1),
        preferredWorkbenchHeight_,
        activeTab,
    });

    const NativeUiMetrics& metrics = layoutModel.metrics;
    const bool compact = metrics.compact;
    const int margin = metrics.margin;
    const int row = metrics.row;
    const int gap = metrics.gap;
    const int labelHeight = metrics.labelHeight;
    const int titleHeight = metrics.titleHeight;
    const int editHeight = singleLineEditHeight(uiFont_, row);
    const int editOffsetY = std::max(0, (row - editHeight) / 2);
    const BOOL layoutRepaint = draggingWorkbenchSplitter_ ? FALSE : TRUE;
    NativeLayoutTransaction layoutTransaction(layoutRepaint);
    NativeLayoutTransaction* activeLayoutTransaction = &layoutTransaction;
    const auto showControl = [&](HWND control, bool visible) {
        activeLayoutTransaction->show(control, visible);
    };
    const auto moveControl = [&](HWND control, int moveX, int moveY, int moveWidth, int moveHeight, BOOL) {
        activeLayoutTransaction->move(control, moveX, moveY, moveWidth, moveHeight);
    };
    const auto moveTopControl = [&](HWND control, int moveX, int moveY, int moveWidth, int moveHeight, BOOL) {
        activeLayoutTransaction->moveTop(control, moveX, moveY, moveWidth, moveHeight);
    };
    const auto moveControlRect = [&](HWND control, const NativeRect& rect) {
        moveControl(control, rect.x, rect.y, rect.width, rect.height, layoutRepaint);
    };
    const int sideWidth = layoutModel.serialPanel.bounds.width;
    const int sideX = layoutModel.serialPanel.bounds.x;
    currentWorkbenchHeight_ = layoutModel.workbench.workbenchHeight;
    workbenchSplitterRect_ = toWinRect(layoutModel.workbench.splitter);

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
    const bool sideActionVisible = layoutModel.serialPanel.actionsVisible;
    moveControlRect(sideActionSeparator_, layoutModel.serialPanel.actionSeparator);
    showControl(sideActionSeparator_, sideActionVisible);
    moveControlRect(pauseScrollButton_, layoutModel.serialPanel.pauseButton);
    moveControlRect(clearButton_, layoutModel.serialPanel.clearButton);
    showControl(pauseScrollButton_, sideActionVisible);
    showControl(clearButton_, sideActionVisible);
    const bool sideHelpVisible = layoutModel.sideHelp.visible;
    moveControlRect(sideHelpSeparator_, layoutModel.sideHelp.separator);
    showControl(sideHelpSeparator_, sideHelpVisible);
    if (sideHelpVisible) {
        moveControlRect(sideHelpFrame_, layoutModel.sideHelp.frame);
        moveControlRect(sideHelpTitle_, layoutModel.sideHelp.title);
        moveControlRect(sideHelpText_, layoutModel.sideHelp.text);
    } else {
        const int sideHelpY = layoutModel.sideHelp.frame.y;
        moveControl(sideHelpFrame_, x, sideHelpY, 1, 1, layoutRepaint);
        moveControl(sideHelpTitle_, x, sideHelpY, 1, 1, layoutRepaint);
        moveControl(sideHelpText_, x, sideHelpY, 1, 1, layoutRepaint);
    }
    showControl(sideHelpFrame_, sideHelpVisible);
    showControl(sideHelpTitle_, sideHelpVisible);
    showControl(sideHelpText_, sideHelpVisible);

    const bool showLogTitle = layoutModel.logPanel.titleVisible;
    showControl(logPanelTitle_, showLogTitle);
    if (showLogTitle) {
        moveControlRect(logPanelTitle_, layoutModel.logPanel.title);
    }
    const LogToolbarLayout& logLayout = layoutModel.logPanel.toolbar;
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
    moveControlRect(receiveLog_, layoutModel.logPanel.logView);

    showControl(workPanelTitle_, false);
    moveControlRect(workTabs_, layoutModel.workbench.tabs);

    const int pageX = layoutModel.workbench.page.x;
    const int pageY = layoutModel.workbench.page.y;
    const int pageBottom = layoutModel.workbench.page.bottom();
    const int pageW = layoutModel.workbench.page.width;
    const int labelOffsetY = std::max(0, (row - labelHeight) / 2);
    const int checkOffsetY = std::max(0, (row - 16) / 2);
    const int progressOffsetY = compact ? 3 : 4;
    const int progressHeight = compact ? 14 : 16;
    const int formFieldGap = compact ? 2 : 3;
    const auto moveWorkEdit = [&](HWND control, int editX, int editY, int editWidth) {
        moveControl(control, editX, editY + editOffsetY, editWidth, editHeight, layoutRepaint);
    };
    moveControlRect(workPageBackground_, layoutModel.workbench.pageBackground);
    const bool showSingleSendFormatRow = layoutModel.workbench.visibility.singleFormatRow;

    showControl(sendModeLabel_, false);
    showControl(sendEncodingLabel_, false);
    showControl(lineEndingLabel_, false);
    showControl(sendHistoryLabel_, false);

    const int formatGapCount = pageW >= 380 ? 3 : 2;
    const int formatAvailable = std::max(3, pageW - gap * formatGapCount);
    const bool showHistoryCombo = layoutModel.workbench.visibility.singleHistory;
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
    const bool sendContentVisible = layoutModel.workbench.visibility.singleSend;
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
    const auto& quickSlotVisible = layoutModel.workbench.visibility.quickSlots;
    for (std::size_t index = 0; index < quickSendEdits_.size(); ++index) {
        const int column = static_cast<int>(index % quickColumns);
        const int slotRow = static_cast<int>(index / quickColumns);
        const int slotX = pageX + column * (quickColumnWidth + gap);
        const int slotY = pageY + slotRow * quickSlotHeight;
        const int editWidth = std::max(1, quickColumnWidth - quickButtonWidth - gap);
        moveWorkEdit(quickSendEdits_[index], slotX, slotY, editWidth);
        moveControl(quickSendButtons_[index], slotX + editWidth + gap, slotY, quickButtonWidth, row, layoutRepaint);
    }

    x = pageX;
    y = pageY;
    const int fileLabelWidth = compact ? 30 : 34;
    const int browseWidth = compact ? 44 : 50;
    const int fileSendWidth = compact ? 68 : 76;
    const int fileStopWidth = compact ? 44 : 50;
    const bool fileFirstRowVisible = layoutModel.workbench.visibility.fileFirstRow;
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
    const bool fileSecondRowVisible = layoutModel.workbench.visibility.fileSecondRow;
    moveControl(fileDelayLabel_, x, y + labelOffsetY, delayLabelWidth, labelHeight, layoutRepaint);
    x += delayLabelWidth + formFieldGap;
    moveControl(fileDelayCombo_, x, y, delayComboWidth, 160, layoutRepaint);
    x += delayComboWidth + gap;
    moveControl(fileProgressLabel_, x, y + labelOffsetY, fileProgressLabelWidth, labelHeight, layoutRepaint);
    x += fileProgressLabelWidth + formFieldGap;
    moveTopControl(fileProgress_, x, y + progressOffsetY, std::max(1, pageX + pageW - x), progressHeight, layoutRepaint);

    showControl(workflowHint_, false);
    y = pageY;
    const int scanSectionGap = compact ? 2 : 3;
    const int scanBlockGap = compact ? 3 : 4;
    const bool scanSectionVisible = layoutModel.workbench.visibility.scanSection;
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
    const bool scanParameterRowVisible = layoutModel.workbench.visibility.scanParameterRow;
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
    const bool scanProgressRowVisible = layoutModel.workbench.visibility.scanProgressRow;
    moveControl(modbusProgressLabel_, x, y + labelOffsetY, progressLabelWidth, labelHeight, layoutRepaint);
    x += progressLabelWidth + gap;
    moveTopControl(modbusProgress_, x, y + progressOffsetY, progressBarWidth, progressHeight, layoutRepaint);
    x += progressBarWidth + gap;
    moveControl(modbusProgressText_, x, y + labelOffsetY, progressTextWidth, labelHeight, layoutRepaint);

    y += row + scanBlockGap;
    x = pageX;
    const bool scanAnalysisSectionVisible = layoutModel.workbench.visibility.scanAnalysisSection;
    moveControl(analysisSectionLabel_, pageX, y, pageW, labelHeight, layoutRepaint);
    y += labelHeight + scanSectionGap;
    const bool scanTargetRowVisible = layoutModel.workbench.visibility.scanTargetRow;
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
    const bool scanCandidateRowVisible = layoutModel.workbench.visibility.scanCandidateRow;
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
    const bool settingsRowVisible = layoutModel.workbench.visibility.settingsRow;
    moveControl(logCacheLabel_, x, y + labelOffsetY, settingsLabelWidth, labelHeight, layoutRepaint);
    x += settingsLabelWidth + formFieldGap;
    moveControl(logCacheCombo_, x, y, settingsComboWidth, 260, layoutRepaint);
    x += settingsComboWidth + gap;
    moveControl(rawEventRetentionLabel_, x, y + labelOffsetY, rawRetentionLabelWidth, labelHeight, layoutRepaint);
    x += rawRetentionLabelWidth + formFieldGap;
    moveControl(rawEventRetentionCombo_, x, y, rawRetentionComboWidth, 140, layoutRepaint);

    const bool pageVisible = layoutModel.workbench.visibility.pageVisible;
    showControl(workTabs_, pageVisible);
    showControl(workPageBackground_, pageVisible);
    layoutTransaction.commit();
    NativeLayoutTransaction statusTransaction(layoutRepaint);
    activeLayoutTransaction = &statusTransaction;
    workbenchVisibility_ = {};
    workbenchVisibility_.pageVisible = pageVisible;
    workbenchVisibility_.singleFormatRow = showSingleSendFormatRow;
    workbenchVisibility_.singleHistory = showHistoryCombo;
    workbenchVisibility_.singleSend = sendContentVisible;
    workbenchVisibility_.singleTimed = layoutModel.workbench.visibility.singleTimed;
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
    workbenchRedrawRect_ = toWinRect(layoutModel.workbench.tabs);
    workbenchVisibilityReady_ = true;
    workbenchTabState_.noteLayoutChanged();
    applyWorkbenchTabVisibility(activeTab);
    const bool showClock = layoutModel.status.clockVisible;
    const bool showCounters = layoutModel.status.countersVisible;
    showControl(clockStatusText_, showClock);
    showControl(rxStatusText_, showCounters);
    showControl(txStatusText_, showCounters);
    if (showClock) {
        moveControlRect(clockStatusText_, layoutModel.status.clockText);
    }
    if (showCounters) {
        moveControlRect(rxStatusText_, layoutModel.status.rxText);
        moveControlRect(txStatusText_, layoutModel.status.txText);
    }
    moveControlRect(statusText_, layoutModel.status.statusText);
    statusTransaction.commit();
}

} // namespace svm::win32

#endif
