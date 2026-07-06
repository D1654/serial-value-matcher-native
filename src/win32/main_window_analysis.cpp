#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_analysis_workflow.h"
#include "win32/native_control_utils.h"
#include "win32/native_time_utils.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"

#include <algorithm>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

void NativeMainWindow::showAnalysisWorkspace() {
    if (modbusAnalysisController_.analysisDecision(store_.isOpen(), false, false, true, true)
        == NativeAnalysisAction::StorageUnavailable) {
        setStatus(tx(T::AnalysisNoStore));
        return;
    }
    const auto session = store_.latestScanSession();
    if (modbusAnalysisController_.analysisDecision(true, session.has_value(), false, true, true)
        == NativeAnalysisAction::ScanRequired) {
        setStatus(tx(T::AnalysisNoScan));
        return;
    }

    const auto observations = store_.scanObservations(session->sessionId);
    if (modbusAnalysisController_.analysisDecision(true, true, !observations.empty(), true, true)
        == NativeAnalysisAction::ObservationsRequired) {
        setStatus(tx(T::AnalysisNoObservations));
        return;
    }

    bool targetOk = false;
    const double targetValue = textToDoubleText(analysisInputText(targetValueEdit_), 0.0, &targetOk);
    if (modbusAnalysisController_.analysisDecision(true, true, true, targetOk, true)
        == NativeAnalysisAction::InvalidTarget) {
        setStatus(tx(T::AnalysisInvalidTarget));
        return;
    }
    bool toleranceOk = false;
    const double tolerance = std::max(0.0, textToDouble(toleranceEdit_, 0.0, &toleranceOk));

    const std::string runId = "match-" + nativeTimestampIdText();
    const NativeCandidateAnalysisBuildResult analysis = nativeBuildCandidateAnalysisRun(
        *session,
        observations,
        wideToUtf8(analysisInputText(targetLabelEdit_)),
        targetValue,
        wideToUtf8(analysisInputText(targetUnitEdit_)),
        toleranceOk ? tolerance : 0.0,
        runId,
        nativeUtcTimestampText(),
        nativeUtcTimestampText(),
        nativeUtcTimestampText());
    if (!analysis.success) {
        setStatus(utf8ToWide(analysis.errorMessage));
        return;
    }
    if (modbusAnalysisController_.analysisDecision(true, true, true, true, !analysis.candidates.empty())
        == NativeAnalysisAction::NoCandidates) {
        setStatus(tx(T::AnalysisNoCandidates));
        return;
    }

    if (!store_.saveMatchRun(analysis.run, analysis.candidates)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return;
    }
    candidateCacheState_.setLatestMatchRunId(runId);
    refreshCandidateCombo(runId);

    const std::wstring summary = uiString(T::AnalysisSavedPrefix)
        + utf8ToWide(session->sessionId)
        + uiString(T::AnalysisSavedMid)
        + std::to_wstring(analysis.candidates.size())
        + uiString(T::AnalysisSavedRun)
        + utf8ToWide(runId)
        + uiString(T::ChinesePeriod);
    appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    setStatus(summary);
}

void NativeMainWindow::showRuleVerification() {
    if (modbusAnalysisController_.ruleVerificationDecision(store_.isOpen(), false, false, false, false, false)
        == NativeRuleAction::StorageUnavailable) {
        setStatus(tx(T::RuleNoStore));
        return;
    }

    const auto candidate = selectedCandidate();
    const bool hasExistingRules = !store_.recentProtocolFieldRules(1).empty();
    if (modbusAnalysisController_.ruleVerificationDecision(true, candidate.has_value(), hasExistingRules, true, true, true)
        == NativeRuleAction::CandidateOrRuleRequired) {
        setStatus(tx(T::RuleNoCandidates));
        return;
    }
    if (candidate.has_value()) {
        if (!saveRuleFromCandidate(*candidate)) {
            return;
        }
    }

    const auto session = store_.latestScanSession();
    if (modbusAnalysisController_.ruleVerificationDecision(true, true, true, session.has_value(), true, true)
        == NativeRuleAction::ScanRequired) {
        setStatus(tx(T::RuleVerifyNoScan));
        return;
    }
    runRuleVerification(*session);
}

void NativeMainWindow::exportReport() {
    if (modbusAnalysisController_.reportDecision(store_.isOpen(), true) == NativeReportAction::StorageUnavailable) {
        setStatus(tx(T::RuleNoStore));
        return;
    }

    std::optional<native_storage::RuleVerificationRunRecord> run;
    if (candidateCacheState_.hasLatestVerificationRunId()) {
        run = store_.ruleVerificationRun(candidateCacheState_.latestVerificationRunId());
    }
    if (!run.has_value()) {
        run = store_.latestRuleVerificationRun();
    }
    if (modbusAnalysisController_.reportDecision(true, run.has_value()) == NativeReportAction::RunRequired) {
        setStatus(tx(T::ExportNoRun));
        return;
    }

    const auto resultRecords = store_.ruleVerificationResults(run->verificationRunId);

    wchar_t fileName[MAX_PATH] = {};
    const std::wstring defaultName = uiString(T::ExportDefaultPrefix)
        + utf8ToWide(run->verificationRunId)
        + L".md";
    wcsncpy_s(fileName, defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = tx(T::ExportFilter);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"md";
    dialog.lpstrTitle = tx(T::ExportDialogTitle);
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&dialog)) {
        return;
    }

    const std::string markdown = nativeRenderRuleVerificationMarkdownReport(*run, resultRecords);
    std::ofstream output(std::filesystem::path(fileName), std::ios::binary | std::ios::trunc);
    if (!output) {
        setStatus(uiString(T::ExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    output.write(markdown.data(), static_cast<std::streamsize>(markdown.size()));
    if (!output) {
        setStatus(uiString(T::ExportFailedPrefix) + std::wstring(fileName));
        return;
    }
    setStatus(uiString(T::ExportOkPrefix) + std::wstring(fileName) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::refreshCandidateCombo(const std::string& runId) {
    SendMessageW(candidateCombo_, CB_RESETCONTENT, 0, 0);
    addComboItem(candidateCombo_, tx(T::CandidatePlaceholder), 0);
    if (!loadCandidateCache(runId)) {
        SendMessageW(candidateCombo_, CB_SETCURSEL, 0, 0);
        return;
    }

    for (const native_storage::MatchCandidateRecord& candidate : candidateCacheState_.candidates()) {
        const std::wstring display = nativeCandidateDisplayText(candidate);
        const LRESULT index = SendMessageW(candidateCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        if (index >= 0) {
            SendMessageW(candidateCombo_, CB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(candidate.id));
        }
    }
    SendMessageW(candidateCombo_, CB_SETCURSEL, candidateCacheState_.candidatesEmpty() ? 0 : 1, 0);
}

bool NativeMainWindow::loadCandidateCache(const std::string& runId) {
    if (!store_.isOpen() || runId.empty()) {
        candidateCacheState_.clearCandidateCache();
        return false;
    }
    if (candidateCacheState_.isCandidateCacheFor(runId)) {
        return true;
    }
    candidateCacheState_.setCandidateCache(runId, store_.matchCandidates(runId));
    return true;
}

std::optional<native_storage::MatchCandidateRecord> NativeMainWindow::selectedCandidate() {
    if (!store_.isOpen()) {
        return std::nullopt;
    }
    std::string runId = candidateCacheState_.latestMatchRunId();
    if (runId.empty()) {
        const auto run = store_.latestMatchRun();
        if (!run.has_value()) {
            return std::nullopt;
        }
        runId = run->runId;
        candidateCacheState_.setLatestMatchRunId(runId);
    }

    if (!loadCandidateCache(runId) || candidateCacheState_.candidatesEmpty()) {
        return std::nullopt;
    }
    const LRESULT index = SendMessageW(candidateCombo_, CB_GETCURSEL, 0, 0);
    const LRESULT itemData = index >= 0 ? SendMessageW(candidateCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) : 0;
    const std::int64_t selectedId = itemData == CB_ERR ? 0 : static_cast<std::int64_t>(itemData);
    if (const auto candidate = candidateCacheState_.candidateById(selectedId); candidate.has_value()) {
        return candidate;
    }
    return candidateCacheState_.defaultCandidate();
}

bool NativeMainWindow::saveRuleFromCandidate(const native_storage::MatchCandidateRecord& candidate) {
    if (!store_.isOpen()) {
        setStatus(tx(T::RuleNoStore));
        return false;
    }
    if (candidate.id <= 0) {
        setStatus(tx(T::RuleNoCandidates));
        return false;
    }

    native_storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = "rule-" + std::to_string(candidate.id);
    rule.fieldName = wideToUtf8(nativeRuleDisplayName(candidate, analysisInputText(targetLabelEdit_)));
    rule.sourceStabilityRunId = candidate.runId;
    rule.sourceStableCandidateId = candidate.id;
    rule.candidateType = candidate.candidateType;
    rule.wordOrder = candidate.wordOrder;
    rule.byteOrder = candidate.byteOrder;
    rule.slaveId = candidate.slaveId;
    rule.functionCode = candidate.functionCode;
    rule.startAddress = candidate.startAddress;
    rule.registerCount = candidate.registerCount;
    rule.scaleMultiplier = candidate.scaleMultiplier;
    rule.scaleOffset = candidate.scaleOffset;
    rule.unit = wideToUtf8(analysisInputText(targetUnitEdit_));
    rule.confidenceLevel = wideToUtf8(candidate.score >= 85.0 ? L"\u9AD8" : (candidate.score >= 65.0 ? L"\u4E2D" : L"\u4F4E"));
    rule.stabilityScore = candidate.score;
    rule.evidenceSummary = candidate.evidenceText;
    rule.createdAtUtc = nativeUtcTimestampText();
    if (!store_.saveProtocolFieldRule(rule)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return false;
    }

    setStatus(uiString(T::RuleSavedPrefix) + utf8ToWide(rule.fieldName) + L" (" + utf8ToWide(rule.ruleId) + L")" + uiString(T::ChinesePeriod));
    return true;
}

bool NativeMainWindow::runRuleVerification(const native_storage::ScanSessionRecord& session) {
    if (modbusAnalysisController_.ruleVerificationDecision(store_.isOpen(), true, true, true, true, true)
        == NativeRuleAction::StorageUnavailable) {
        setStatus(tx(T::RuleNoStore));
        return false;
    }

    const auto rules = store_.recentProtocolFieldRules(200);
    if (modbusAnalysisController_.ruleVerificationDecision(true, true, true, true, !rules.empty(), true)
        == NativeRuleAction::RulesRequired) {
        setStatus(tx(T::RuleVerifyNoRules));
        return false;
    }
    const auto observations = store_.scanObservations(session.sessionId);
    if (modbusAnalysisController_.ruleVerificationDecision(true, true, true, true, true, !observations.empty())
        == NativeRuleAction::ObservationsRequired) {
        setStatus(tx(T::RuleVerifyNoObservations));
        return false;
    }

    const NativeRuleVerificationBuildResult verification = nativeBuildRuleVerificationResult(
        session,
        rules,
        observations,
        "verify-" + nativeTimestampIdText(),
        nativeUtcTimestampText());

    if (!store_.saveRuleVerificationRun(verification.run, verification.results)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return false;
    }
    candidateCacheState_.setLatestVerificationRunId(verification.run.verificationRunId);

    const std::wstring summary = uiString(T::RuleVerifySavedPrefix)
        + utf8ToWide(verification.run.verificationRunId)
        + uiString(T::RulesTotalPrefix)
        + std::to_wstring(verification.run.ruleCount)
        + uiString(T::RulesVerifiedPrefix)
        + std::to_wstring(verification.run.verifiedCount)
        + uiString(T::RulesMissingPrefix)
        + std::to_wstring(verification.run.missingCount)
        + uiString(T::RulesUnsupportedPrefix)
        + std::to_wstring(verification.run.unsupportedCount)
        + uiString(T::ChinesePeriod);
    appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    setStatus(summary);
    return true;
}

} // namespace svm::win32

#endif
