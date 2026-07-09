#include "core/dangerous_operation_policy.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using svm::core::Byte;
using svm::core::DangerousOperationConfirmationState;
using svm::core::DangerousOperationKind;
using svm::core::DangerousOperationRequest;

void manualSingleWriteDoesNotRequireConfirmation() {
    DangerousOperationRequest request;
    request.kind = DangerousOperationKind::ManualSerialWrite;
    request.payloadBytes = 8;

    const auto policy = svm::core::evaluateDangerousOperation(request);

    assert(!policy.requiresConfirmation);
    assert(policy.metadata.at("requires_confirmation") == "false");
    assert(policy.metadata.at("operation_kind") == "manual_serial_write");
}

void batchAndAutomatedWritesRequireConfirmation() {
    DangerousOperationRequest fileSend;
    fileSend.kind = DangerousOperationKind::FileSerialSend;
    fileSend.payloadBytes = 1024;

    DangerousOperationRequest timedSend;
    timedSend.kind = DangerousOperationKind::TimedSerialWrite;
    timedSend.payloadBytes = 4;
    timedSend.repeatCount = 10;

    DangerousOperationRequest sequence;
    sequence.kind = DangerousOperationKind::CommandSequenceWrite;
    sequence.payloadBytes = 32;
    sequence.commandCount = 5;

    assert(svm::core::evaluateDangerousOperation(fileSend).requiresConfirmation);
    assert(svm::core::evaluateDangerousOperation(timedSend).requiresConfirmation);
    assert(svm::core::evaluateDangerousOperation(sequence).requiresConfirmation);
}

void modbusWriteRulesAreConservative() {
    DangerousOperationRequest read;
    read.kind = DangerousOperationKind::ManualSerialWrite;
    read.slaveId = 1;
    read.functionCode = 0x03;

    DangerousOperationRequest holdingRegisterWrite;
    holdingRegisterWrite.kind = DangerousOperationKind::ManualSerialWrite;
    holdingRegisterWrite.slaveId = 1;
    holdingRegisterWrite.functionCode = 0x06;
    holdingRegisterWrite.address = 100;
    holdingRegisterWrite.quantity = 1;

    DangerousOperationRequest broadcastWrite = holdingRegisterWrite;
    broadcastWrite.slaveId = 0;

    const auto readPolicy = svm::core::evaluateDangerousOperation(read);
    const auto registerWritePolicy = svm::core::evaluateDangerousOperation(holdingRegisterWrite);
    const auto broadcastPolicy = svm::core::evaluateDangerousOperation(broadcastWrite);

    assert(!readPolicy.requiresConfirmation);
    assert(registerWritePolicy.requiresConfirmation);
    assert(registerWritePolicy.kind == DangerousOperationKind::ModbusRegisterWrite);
    assert(registerWritePolicy.metadata.at("operation_kind") == "modbus_register_write");
    assert(broadcastPolicy.requiresConfirmation);
    assert(broadcastPolicy.kind == DangerousOperationKind::ModbusBroadcastWrite);
    assert(broadcastPolicy.metadata.at("operation_kind") == "modbus_broadcast_write");
    assert(broadcastPolicy.metadata.at("is_broadcast_slave") == "true");
}

void dialogResultsFailClosed() {
    assert(svm::core::dangerousOperationConfirmationStateFromDialogResult(6) == DangerousOperationConfirmationState::Confirmed);
    assert(svm::core::dangerousOperationConfirmationStateFromDialogResult(1) == DangerousOperationConfirmationState::Confirmed);
    assert(svm::core::dangerousOperationConfirmationStateFromDialogResult(7) == DangerousOperationConfirmationState::Cancelled);
    assert(svm::core::dangerousOperationConfirmationStateFromDialogResult(2) == DangerousOperationConfirmationState::Cancelled);
    assert(svm::core::dangerousOperationConfirmationStateFromDialogResult(0) == DangerousOperationConfirmationState::PromptFailed);
    assert(svm::core::dangerousOperationConfirmationStateFromDialogResult(12345) == DangerousOperationConfirmationState::Cancelled);

    assert(svm::core::dangerousOperationConfirmationSatisfied(DangerousOperationConfirmationState::Confirmed));
    assert(svm::core::dangerousOperationConfirmationSatisfied(DangerousOperationConfirmationState::NotRequired));
    assert(!svm::core::dangerousOperationConfirmationSatisfied(DangerousOperationConfirmationState::Cancelled));
    assert(!svm::core::dangerousOperationConfirmationSatisfied(DangerousOperationConfirmationState::PromptFailed));
}

void auditEventIncludesRedactionSafeMetadata() {
    DangerousOperationRequest request;
    request.kind = DangerousOperationKind::FileSerialSend;
    request.payloadBytes = 4096;

    const auto policy = svm::core::evaluateDangerousOperation(request);
    const auto audit = svm::core::makeDangerousOperationAuditEvent(
        policy,
        request.kind,
        DangerousOperationConfirmationState::Cancelled);

    assert(audit.kind == DangerousOperationKind::FileSerialSend);
    assert(audit.state == DangerousOperationConfirmationState::Cancelled);
    assert(audit.metadata.at("operation_kind") == "file_serial_send");
    assert(audit.metadata.at("payload_bytes") == "4096");
    assert(audit.metadata.at("confirmation_result") == "cancelled");
    assert(audit.summary.find("file_serial_send") != std::string::npos);
}

void auditEventUsesFinalPolicyClassification() {
    DangerousOperationRequest request;
    request.kind = DangerousOperationKind::ManualSerialWrite;
    request.slaveId = 17;
    request.functionCode = 0x06;
    request.address = 10;
    request.quantity = 1;

    const auto policy = svm::core::evaluateDangerousOperation(request);
    const auto audit = svm::core::makeDangerousOperationAuditEvent(
        policy,
        request.kind,
        DangerousOperationConfirmationState::Confirmed);

    assert(audit.kind == DangerousOperationKind::ModbusRegisterWrite);
    assert(audit.metadata.at("operation_kind") == "modbus_register_write");
}

void stableNamesRemainTokenLike() {
    assert(std::string(svm::core::dangerousOperationKindName(DangerousOperationKind::ManualSerialWrite)) == "manual_serial_write");
    assert(std::string(svm::core::dangerousOperationKindName(DangerousOperationKind::ModbusBroadcastWrite)) == "modbus_broadcast_write");
    assert(std::string(svm::core::dangerousOperationConfirmationStateName(DangerousOperationConfirmationState::PromptFailed)) == "prompt_failed");
    assert(svm::core::isModbusWriteFunction(static_cast<Byte>(0x05)));
    assert(svm::core::isModbusWriteFunction(static_cast<Byte>(0x10)));
    assert(!svm::core::isModbusWriteFunction(static_cast<Byte>(0x03)));
}

} // namespace

int main() {
    manualSingleWriteDoesNotRequireConfirmation();
    batchAndAutomatedWritesRequireConfirmation();
    modbusWriteRulesAreConservative();
    dialogResultsFailClosed();
    auditEventIncludesRedactionSafeMetadata();
    auditEventUsesFinalPolicyClassification();
    stableNamesRemainTokenLike();

    std::cout << "dangerous_operation_policy_tests passed\n";
    return 0;
}
