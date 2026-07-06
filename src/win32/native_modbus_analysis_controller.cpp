#include "win32/native_modbus_analysis_controller.h"

namespace svm::win32 {

bool NativeModbusScanDecision::startsScan() const noexcept {
    return action == NativeModbusAction::StartScan;
}

bool NativeModbusScanDecision::cancelsScan() const noexcept {
    return action == NativeModbusAction::CancelScan;
}

NativeModbusScanDecision NativeModbusAnalysisController::scanDecision(
    bool scanRunning,
    bool serialOpen,
    bool storeOpen,
    const NativeSerialIoState& ioState) const noexcept {
    if (scanRunning) {
        return {NativeModbusAction::CancelScan, NativeSerialIoOwner::ModbusScan};
    }
    if (!serialOpen) {
        return {NativeModbusAction::ConnectRequired};
    }
    if (!storeOpen) {
        return {NativeModbusAction::StorageUnavailable};
    }
    if (!ioState.allowsOwner(NativeSerialIoOwner::ModbusScan)) {
        return {NativeModbusAction::SerialBusy};
    }
    return {NativeModbusAction::StartScan, NativeSerialIoOwner::ModbusScan};
}

NativeModbusScanUiSnapshot NativeModbusAnalysisController::scanUiSnapshot(const NativeModbusScanUiState& state, bool running) const noexcept {
    return state.snapshot(running);
}

NativeAnalysisAction NativeModbusAnalysisController::analysisDecision(
    bool storeOpen,
    bool hasScanSession,
    bool hasObservations,
    bool targetValid,
    bool hasCandidates) const noexcept {
    if (!storeOpen) {
        return NativeAnalysisAction::StorageUnavailable;
    }
    if (!hasScanSession) {
        return NativeAnalysisAction::ScanRequired;
    }
    if (!hasObservations) {
        return NativeAnalysisAction::ObservationsRequired;
    }
    if (!targetValid) {
        return NativeAnalysisAction::InvalidTarget;
    }
    if (!hasCandidates) {
        return NativeAnalysisAction::NoCandidates;
    }
    return NativeAnalysisAction::RunAnalysis;
}

NativeRuleAction NativeModbusAnalysisController::ruleVerificationDecision(
    bool storeOpen,
    bool hasSelectedCandidate,
    bool hasExistingRules,
    bool hasScanSession,
    bool hasRulesForVerification,
    bool hasObservations) const noexcept {
    if (!storeOpen) {
        return NativeRuleAction::StorageUnavailable;
    }
    if (!hasSelectedCandidate && !hasExistingRules) {
        return NativeRuleAction::CandidateOrRuleRequired;
    }
    if (!hasScanSession) {
        return NativeRuleAction::ScanRequired;
    }
    if (!hasRulesForVerification) {
        return NativeRuleAction::RulesRequired;
    }
    if (!hasObservations) {
        return NativeRuleAction::ObservationsRequired;
    }
    return hasSelectedCandidate ? NativeRuleAction::SaveCandidateRule : NativeRuleAction::RunVerification;
}

NativeReportAction NativeModbusAnalysisController::reportDecision(bool storeOpen, bool hasRun) const noexcept {
    if (!storeOpen) {
        return NativeReportAction::StorageUnavailable;
    }
    if (!hasRun) {
        return NativeReportAction::RunRequired;
    }
    return NativeReportAction::Export;
}

NativePreferenceSaveDecision NativeModbusAnalysisController::preferenceSaveDecision(
    bool storeOpen,
    bool windowReady,
    bool minimized) const noexcept {
    if (!storeOpen || !windowReady) {
        return {};
    }
    if (minimized) {
        return {false, true};
    }
    return {true, false};
}

int NativeModbusAnalysisController::normalizedRawEventRetentionLimitMb(int retentionLimitMb) const noexcept {
    return nativeNormalizeRawEventRetentionMb(retentionLimitMb);
}

std::uintmax_t NativeModbusAnalysisController::rawEventSoftLimitBytes(int retentionLimitMb) const noexcept {
    const int normalized = normalizedRawEventRetentionLimitMb(retentionLimitMb);
    return normalized <= 0 ? 0 : static_cast<std::uintmax_t>(normalized) * 1024ULL * 1024ULL;
}

std::uintmax_t NativeModbusAnalysisController::rawEventTargetLimitBytes(int retentionLimitMb) const noexcept {
    return rawEventSoftLimitBytes(retentionLimitMb) * 4ULL / 5ULL;
}

bool NativeModbusAnalysisController::shouldPersistPreferences(
    const native_storage::UiPreferences* lastSaved,
    const native_storage::UiPreferences& next) const {
    return lastSaved == nullptr || !nativeUiPreferencesSameSettings(*lastSaved, next);
}

} // namespace svm::win32
