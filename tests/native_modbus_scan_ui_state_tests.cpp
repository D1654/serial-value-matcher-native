#include "win32/native_modbus_analysis_controller.h"
#include "win32/native_modbus_scan_ui_state.h"

#include <cassert>
#include <iostream>

namespace {

void idleSnapshotKeepsControlsEditable() {
    svm::win32::NativeModbusScanUiState state;
    const auto ui = state.snapshot(false);
    assert(ui.buttonMode == svm::win32::NativeModbusScanButtonMode::Scan);
    assert(ui.exclusiveControlsEnabled);
}

void runningSnapshotSwitchesToStopAndLocksExclusiveControls() {
    svm::win32::NativeModbusScanUiState state;
    const auto ui = state.snapshot(true);
    assert(ui.buttonMode == svm::win32::NativeModbusScanButtonMode::Stop);
    assert(!ui.exclusiveControlsEnabled);
}

void controllerRoutesScanStartCancelAndGates() {
    svm::win32::NativeModbusAnalysisController controller;
    svm::win32::NativeSerialIoState ioState;

    auto decision = controller.scanDecision(true, true, true, ioState);
    assert(decision.cancelsScan());
    assert(decision.owner == svm::win32::NativeSerialIoOwner::ModbusScan);

    decision = controller.scanDecision(false, false, true, ioState);
    assert(decision.action == svm::win32::NativeModbusAction::ConnectRequired);

    decision = controller.scanDecision(false, true, false, ioState);
    assert(decision.action == svm::win32::NativeModbusAction::StorageUnavailable);

    assert(ioState.tryAcquire(svm::win32::NativeSerialIoOwner::FileSend));
    decision = controller.scanDecision(false, true, true, ioState);
    assert(decision.action == svm::win32::NativeModbusAction::SerialBusy);

    ioState.forceRelease();
    decision = controller.scanDecision(false, true, true, ioState);
    assert(decision.startsScan());
    assert(decision.owner == svm::win32::NativeSerialIoOwner::ModbusScan);
}

void controllerKeepsAnalysisAndReportDecisionsPure() {
    svm::win32::NativeModbusAnalysisController controller;

    assert(controller.analysisDecision(false, false, false, true, true) == svm::win32::NativeAnalysisAction::StorageUnavailable);
    assert(controller.analysisDecision(true, false, false, true, true) == svm::win32::NativeAnalysisAction::ScanRequired);
    assert(controller.analysisDecision(true, true, false, true, true) == svm::win32::NativeAnalysisAction::ObservationsRequired);
    assert(controller.analysisDecision(true, true, true, false, true) == svm::win32::NativeAnalysisAction::InvalidTarget);
    assert(controller.analysisDecision(true, true, true, true, false) == svm::win32::NativeAnalysisAction::NoCandidates);
    assert(controller.analysisDecision(true, true, true, true, true) == svm::win32::NativeAnalysisAction::RunAnalysis);

    assert(controller.ruleVerificationDecision(false, false, false, false, false, false)
        == svm::win32::NativeRuleAction::StorageUnavailable);
    assert(controller.ruleVerificationDecision(true, false, false, true, true, true)
        == svm::win32::NativeRuleAction::CandidateOrRuleRequired);
    assert(controller.ruleVerificationDecision(true, true, false, false, true, true)
        == svm::win32::NativeRuleAction::ScanRequired);
    assert(controller.ruleVerificationDecision(true, false, true, true, true, true)
        == svm::win32::NativeRuleAction::RunVerification);
    assert(controller.ruleVerificationDecision(true, true, true, true, true, true)
        == svm::win32::NativeRuleAction::SaveCandidateRule);

    assert(controller.reportDecision(false, true) == svm::win32::NativeReportAction::StorageUnavailable);
    assert(controller.reportDecision(true, false) == svm::win32::NativeReportAction::RunRequired);
    assert(controller.reportDecision(true, true) == svm::win32::NativeReportAction::Export);
}

} // namespace

int main() {
    idleSnapshotKeepsControlsEditable();
    runningSnapshotSwitchesToStopAndLocksExclusiveControls();
    controllerRoutesScanStartCancelAndGates();
    controllerKeepsAnalysisAndReportDecisionsPure();

    std::cout << "native_modbus_scan_ui_state_tests passed\n";
    return 0;
}
