#include "win32/native_modbus_analysis_controller.h"
#include "win32/native_modbus_scan_ui_state.h"

#include <cassert>
#include <cwchar>
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

void attemptResultClassifierNormalizesNativeStatuses() {
    using svm::win32::NativeModbusAttemptResultKind;
    using svm::win32::classifyNativeModbusAttemptResult;

    assert(classifyNativeModbusAttemptResult("success", false, "") == NativeModbusAttemptResultKind::Success);
    assert(classifyNativeModbusAttemptResult("timeout", false, "等待 Modbus 响应超时。") == NativeModbusAttemptResultKind::Timeout);
    assert(classifyNativeModbusAttemptResult("retry-exhausted", false, "timeout") == NativeModbusAttemptResultKind::RetryExhausted);
    assert(classifyNativeModbusAttemptResult("parse-error", true, "设备返回 Modbus 异常") == NativeModbusAttemptResultKind::ModbusException);
    assert(classifyNativeModbusAttemptResult("parse-error", false, "RTU CRC check failed.") == NativeModbusAttemptResultKind::FrameError);
    assert(classifyNativeModbusAttemptResult("parse-error", false, "响应寄存器数量不匹配") == NativeModbusAttemptResultKind::DataFormatError);
    assert(classifyNativeModbusAttemptResult("parse-error", false, "功能码不匹配") == NativeModbusAttemptResultKind::ProtocolMismatch);
    assert(classifyNativeModbusAttemptResult("transport-error", false, "串口未打开") == NativeModbusAttemptResultKind::TransportError);
    assert(classifyNativeModbusAttemptResult("cancelled", false, "") == NativeModbusAttemptResultKind::Cancelled);
}

void attemptResultLabelsAndCountersAreStable() {
    using svm::win32::NativeModbusAttemptResultKind;

    assert(std::wcscmp(
        svm::win32::nativeModbusAttemptResultLabel(NativeModbusAttemptResultKind::ModbusException),
        L"Modbus 异常") == 0);
    assert(svm::win32::nativeModbusAttemptCountsAsSuccess(NativeModbusAttemptResultKind::Success));
    assert(!svm::win32::nativeModbusAttemptCountsAsFailure(NativeModbusAttemptResultKind::Success));
    assert(!svm::win32::nativeModbusAttemptCountsAsFailure(NativeModbusAttemptResultKind::Cancelled));
    assert(svm::win32::nativeModbusAttemptCountsAsFailure(NativeModbusAttemptResultKind::Timeout));
    assert(svm::win32::nativeModbusAttemptCountsAsFailure(NativeModbusAttemptResultKind::TransportError));
}

void scanMessagesCannotCrossSessionGenerations() {
    svm::transport::SerialSessionSnapshot session;
    session.state = svm::transport::SerialSessionState::Open;
    session.generation = 7;
    assert(svm::win32::nativeModbusScanMessageMatchesSession(7, 7, session, false));
    assert(!svm::win32::nativeModbusScanMessageMatchesSession(6, 7, session, false));
    assert(!svm::win32::nativeModbusScanMessageMatchesSession(7, 8, session, false));
    assert(!svm::win32::nativeModbusScanMessageMatchesSession(0, 7, session, true));

    session.state = svm::transport::SerialSessionState::Closed;
    session.generation = 0;
    assert(!svm::win32::nativeModbusScanMessageMatchesSession(7, 7, session, true));

    session.state = svm::transport::SerialSessionState::Faulted;
    assert(!svm::win32::nativeModbusScanMessageMatchesSession(7, 7, session, false));
    assert(svm::win32::nativeModbusScanMessageMatchesSession(7, 7, session, true));
    assert(!svm::win32::nativeModbusScanMessageMatchesSession(7, 8, session, true));
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
    attemptResultClassifierNormalizesNativeStatuses();
    attemptResultLabelsAndCountersAreStable();
    scanMessagesCannotCrossSessionGenerations();
    controllerRoutesScanStartCancelAndGates();
    controllerKeepsAnalysisAndReportDecisionsPure();

    std::cout << "native_modbus_scan_ui_state_tests passed\n";
    return 0;
}
