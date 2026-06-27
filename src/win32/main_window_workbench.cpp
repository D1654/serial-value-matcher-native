#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/ui_text.h"

#include <commctrl.h>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

void NativeMainWindow::setDefaultFonts() {
    for (HWND child = GetWindow(window_, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        addControlFont(child, uiFont_);
        applyClassicControlChrome(child);
    }
}

void NativeMainWindow::hideWorkbenchTabControls() {
    for (HWND control : {
             sendModeCombo_,
             textEncodingCombo_,
             lineEndingCombo_,
             historyCombo_,
             sendEdit_,
             sendButton_,
             timedSendCheck_,
             timedPeriodLabel_,
             timedPeriodEdit_,
             filePathLabel_,
             filePathEdit_,
             fileBrowseButton_,
             fileSendButton_,
             fileStopButton_,
             fileDelayLabel_,
             fileDelayCombo_,
             fileProgressLabel_,
             fileProgress_,
             workflowHint_,
             scanSectionLabel_,
             scanSlaveLabel_,
             scanSlaveEdit_,
             scanFunctionLabel_,
             scanFunctionCombo_,
             scanStartLabel_,
             scanStartEdit_,
             scanEndLabel_,
             scanEndEdit_,
             modbusProgressLabel_,
             modbusProgress_,
             modbusProgressText_,
             modbusButton_,
             analysisSectionLabel_,
             targetStatic_,
             targetLabelEdit_,
             targetValueStatic_,
             targetValueEdit_,
             targetUnitStatic_,
             targetUnitEdit_,
             toleranceStatic_,
             toleranceEdit_,
             candidateStatic_,
             candidateCombo_,
             analysisButton_,
             ruleVerifyButton_,
             exportReportButton_,
             logCacheLabel_,
             logCacheCombo_,
             rawEventRetentionLabel_,
             rawEventRetentionCombo_,
         }) {
        showControlFast(control, false);
    }
    for (HWND control : quickSendEdits_) {
        showControlFast(control, false);
    }
    for (HWND control : quickSendButtons_) {
        showControlFast(control, false);
    }
}

void NativeMainWindow::setWorkbenchTabControlsVisible(int tabIndex, bool visible) {
    const auto showCached = [&](HWND control, bool cachedVisible) {
        showControlFast(control, visible && cachedVisible);
    };

    if (tabIndex == 0) {
        showCached(sendModeCombo_, workbenchVisibility_.singleFormatRow);
        showCached(textEncodingCombo_, workbenchVisibility_.singleFormatRow);
        showCached(lineEndingCombo_, workbenchVisibility_.singleFormatRow);
        showCached(historyCombo_, workbenchVisibility_.singleHistory);
        showCached(sendEdit_, workbenchVisibility_.singleSend);
        showCached(sendButton_, workbenchVisibility_.singleSend);
        showCached(timedSendCheck_, workbenchVisibility_.singleTimed);
        showCached(timedPeriodLabel_, workbenchVisibility_.singleTimed);
        showCached(timedPeriodEdit_, workbenchVisibility_.singleTimed);
    } else if (tabIndex == 1) {
        for (std::size_t index = 0; index < quickSendEdits_.size(); ++index) {
            showCached(quickSendEdits_[index], workbenchVisibility_.quickSlots[index]);
            showCached(quickSendButtons_[index], workbenchVisibility_.quickSlots[index]);
        }
    } else if (tabIndex == 2) {
        showCached(filePathLabel_, workbenchVisibility_.fileFirstRow);
        showCached(filePathEdit_, workbenchVisibility_.fileFirstRow);
        showCached(fileBrowseButton_, workbenchVisibility_.fileFirstRow);
        showCached(fileSendButton_, workbenchVisibility_.fileFirstRow);
        showCached(fileStopButton_, workbenchVisibility_.fileFirstRow);
        showCached(fileDelayLabel_, workbenchVisibility_.fileSecondRow);
        showCached(fileDelayCombo_, workbenchVisibility_.fileSecondRow);
        showCached(fileProgressLabel_, workbenchVisibility_.fileSecondRow);
        showCached(fileProgress_, workbenchVisibility_.fileSecondRow);
    } else if (tabIndex == 3) {
        showCached(scanSectionLabel_, workbenchVisibility_.scanSection);
        showCached(scanSlaveLabel_, workbenchVisibility_.scanParameterRow);
        showCached(scanSlaveEdit_, workbenchVisibility_.scanParameterRow);
        showCached(scanFunctionLabel_, workbenchVisibility_.scanParameterRow);
        showCached(scanFunctionCombo_, workbenchVisibility_.scanParameterRow);
        showCached(scanStartLabel_, workbenchVisibility_.scanParameterRow);
        showCached(scanStartEdit_, workbenchVisibility_.scanParameterRow);
        showCached(scanEndLabel_, workbenchVisibility_.scanParameterRow);
        showCached(scanEndEdit_, workbenchVisibility_.scanParameterRow);
        showCached(modbusButton_, workbenchVisibility_.scanParameterRow);
        showCached(modbusProgressLabel_, workbenchVisibility_.scanProgressRow);
        showCached(modbusProgress_, workbenchVisibility_.scanProgressRow);
        showCached(modbusProgressText_, workbenchVisibility_.scanProgressRow);
        showCached(analysisSectionLabel_, workbenchVisibility_.scanAnalysisSection);
        showCached(targetStatic_, workbenchVisibility_.scanTargetRow);
        showCached(targetLabelEdit_, workbenchVisibility_.scanTargetRow);
        showCached(targetValueStatic_, workbenchVisibility_.scanTargetRow);
        showCached(targetValueEdit_, workbenchVisibility_.scanTargetRow);
        showCached(targetUnitStatic_, workbenchVisibility_.scanTargetRow);
        showCached(targetUnitEdit_, workbenchVisibility_.scanTargetRow);
        showCached(toleranceStatic_, workbenchVisibility_.scanTargetRow);
        showCached(toleranceEdit_, workbenchVisibility_.scanTargetRow);
        showCached(candidateStatic_, workbenchVisibility_.scanCandidateRow);
        showCached(candidateCombo_, workbenchVisibility_.scanCandidateRow);
        showCached(analysisButton_, workbenchVisibility_.scanCandidateRow);
        showCached(ruleVerifyButton_, workbenchVisibility_.scanCandidateRow);
        showCached(exportReportButton_, workbenchVisibility_.scanCandidateRow);
    } else if (tabIndex == 4) {
        showCached(logCacheLabel_, workbenchVisibility_.settingsRow);
        showCached(logCacheCombo_, workbenchVisibility_.settingsRow);
        showCached(rawEventRetentionLabel_, workbenchVisibility_.settingsRow);
        showCached(rawEventRetentionCombo_, workbenchVisibility_.settingsRow);
    }
}

void NativeMainWindow::updateSideHelp(int tabIndex) {
    if (sideHelpTitle_ == nullptr || sideHelpText_ == nullptr) {
        return;
    }

    if (!workbenchTabState_.shouldUpdateHelp(tabIndex)) {
        return;
    }

    TextId helpId = T::SideHelpSingle;
    switch (workbenchTabState_.helpTopicForTab(tabIndex)) {
    case NativeWorkbenchHelpTopic::Quick:
        helpId = T::SideHelpQuick;
        break;
    case NativeWorkbenchHelpTopic::File:
        helpId = T::SideHelpFile;
        break;
    case NativeWorkbenchHelpTopic::Scan:
        helpId = T::SideHelpScan;
        break;
    case NativeWorkbenchHelpTopic::Settings:
        helpId = T::SideHelpSettings;
        break;
    case NativeWorkbenchHelpTopic::Single:
    default:
        helpId = T::SideHelpSingle;
        break;
    }

    setControlText(sideHelpTitle_, tx(T::SideHelpTitle));
    setControlText(sideHelpText_, tx(helpId));
    workbenchTabState_.markHelpUpdated(tabIndex);
}

void NativeMainWindow::applyWorkbenchTabVisibility(int tabIndex) {
    const bool canRedrawWorkbench = workbenchRedrawRect_.right > workbenchRedrawRect_.left
        && workbenchRedrawRect_.bottom > workbenchRedrawRect_.top;
    const bool suspendRedraw = canRedrawWorkbench && IsWindowVisible(window_) != FALSE;
    const NativeWorkbenchTabApplyPlan plan = workbenchTabState_.beginApply(
        tabIndex,
        workbenchVisibilityReady_,
        workbenchVisibility_.pageVisible,
        canRedrawWorkbench,
        suspendRedraw);
    if (plan.skip) {
        return;
    }

    if (suspendRedraw) {
        SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }
    const auto resumeRedraw = [&]() {
        if (suspendRedraw) {
            SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(window_, &workbenchRedrawRect_, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
    };

    updateSideHelp(tabIndex);
    hideWorkbenchTabControls();

    if (!plan.showRequestedTab) {
        workbenchTabState_.finishApply(tabIndex);
        resumeRedraw();
        return;
    }

    setWorkbenchTabControlsVisible(tabIndex, true);

    workbenchTabState_.finishApply(tabIndex);
    resumeRedraw();
}

void NativeMainWindow::updateWorkbenchTab() {
    int tabIndex = static_cast<int>(TabCtrl_GetCurSel(workTabs_));
    if (tabIndex < 0) {
        tabIndex = 0;
    }
    applyWorkbenchTabVisibility(tabIndex);
}

} // namespace svm::win32

#endif
