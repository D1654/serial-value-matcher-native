#include "core/dangerous_operation_policy.h"

#include <sstream>
#include <utility>

namespace svm::core {
namespace {

constexpr int kDialogResultOk = 1;
constexpr int kDialogResultCancel = 2;
constexpr int kDialogResultYes = 6;
constexpr int kDialogResultNo = 7;

Text toText(std::size_t value) {
    return std::to_string(value);
}

Text toText(int value) {
    return std::to_string(value);
}

Text boolText(bool value) {
    return value ? "true" : "false";
}

Text baseSummary(const DangerousOperationRequest& request) {
    std::ostringstream output;
    output << dangerousOperationKindName(request.kind);
    switch (request.kind) {
    case DangerousOperationKind::ManualSerialWrite:
    case DangerousOperationKind::TimedSerialWrite:
        output << " bytes=" << request.payloadBytes;
        if (request.repeatCount > 1) {
            output << " repeat=" << request.repeatCount;
        }
        break;
    case DangerousOperationKind::FileSerialSend:
        output << " bytes=" << request.payloadBytes;
        break;
    case DangerousOperationKind::CommandSequenceWrite:
        output << " commands=" << request.commandCount;
        output << " bytes=" << request.payloadBytes;
        break;
    case DangerousOperationKind::ModbusBroadcastWrite:
    case DangerousOperationKind::ModbusRegisterWrite:
        output << " slave=" << request.slaveId;
        output << " fc=" << static_cast<int>(request.functionCode);
        output << " address=" << request.address;
        output << " quantity=" << request.quantity;
        break;
    }
    return output.str();
}

std::map<Text, Text> baseMetadata(const DangerousOperationRequest& request) {
    std::map<Text, Text> metadata;
    metadata["operation_kind"] = dangerousOperationKindName(request.kind);
    metadata["payload_bytes"] = toText(request.payloadBytes);
    metadata["command_count"] = toText(request.commandCount);
    metadata["repeat_count"] = toText(request.repeatCount);
    metadata["slave_id"] = toText(request.slaveId);
    metadata["function_code"] = toText(static_cast<int>(request.functionCode));
    metadata["address"] = toText(request.address);
    metadata["quantity"] = toText(request.quantity);
    metadata["is_modbus_write_function"] = boolText(isModbusWriteFunction(request.functionCode));
    metadata["is_broadcast_slave"] = boolText(request.slaveId == 0);
    return metadata;
}

DangerousOperationPolicyResult required(DangerousOperationRequest request, Text reason) {
    DangerousOperationPolicyResult result;
    result.kind = request.kind;
    result.requiresConfirmation = true;
    result.reason = std::move(reason);
    result.summary = baseSummary(request);
    result.metadata = baseMetadata(request);
    result.metadata["requires_confirmation"] = "true";
    result.metadata["confirmation_reason"] = result.reason;
    return result;
}

DangerousOperationPolicyResult safe(DangerousOperationRequest request) {
    DangerousOperationPolicyResult result;
    result.kind = request.kind;
    result.requiresConfirmation = false;
    result.summary = baseSummary(request);
    result.metadata = baseMetadata(request);
    result.metadata["requires_confirmation"] = "false";
    return result;
}

} // namespace

const char* dangerousOperationKindName(DangerousOperationKind kind) noexcept {
    switch (kind) {
    case DangerousOperationKind::ManualSerialWrite:
        return "manual_serial_write";
    case DangerousOperationKind::TimedSerialWrite:
        return "timed_serial_write";
    case DangerousOperationKind::FileSerialSend:
        return "file_serial_send";
    case DangerousOperationKind::CommandSequenceWrite:
        return "command_sequence_write";
    case DangerousOperationKind::ModbusBroadcastWrite:
        return "modbus_broadcast_write";
    case DangerousOperationKind::ModbusRegisterWrite:
        return "modbus_register_write";
    }
    return "unknown";
}

const char* dangerousOperationConfirmationStateName(DangerousOperationConfirmationState state) noexcept {
    switch (state) {
    case DangerousOperationConfirmationState::NotRequired:
        return "not_required";
    case DangerousOperationConfirmationState::Required:
        return "required";
    case DangerousOperationConfirmationState::Confirmed:
        return "confirmed";
    case DangerousOperationConfirmationState::Cancelled:
        return "cancelled";
    case DangerousOperationConfirmationState::PromptFailed:
        return "prompt_failed";
    }
    return "unknown";
}

bool isModbusWriteFunction(Byte functionCode) noexcept {
    return functionCode == 0x05
        || functionCode == 0x06
        || functionCode == 0x0F
        || functionCode == 0x10;
}

bool isDangerousRegisterWriteFunction(Byte functionCode) noexcept {
    return functionCode == 0x06 || functionCode == 0x10;
}

bool dangerousOperationConfirmationSatisfied(DangerousOperationConfirmationState state) noexcept {
    return state == DangerousOperationConfirmationState::NotRequired
        || state == DangerousOperationConfirmationState::Confirmed;
}

DangerousOperationConfirmationState dangerousOperationConfirmationStateFromDialogResult(int dialogResult) noexcept {
    switch (dialogResult) {
    case kDialogResultOk:
    case kDialogResultYes:
        return DangerousOperationConfirmationState::Confirmed;
    case kDialogResultCancel:
    case kDialogResultNo:
        return DangerousOperationConfirmationState::Cancelled;
    case 0:
        return DangerousOperationConfirmationState::PromptFailed;
    default:
        return DangerousOperationConfirmationState::Cancelled;
    }
}

DangerousOperationPolicyResult evaluateDangerousOperation(const DangerousOperationRequest& request) {
    if (request.kind == DangerousOperationKind::FileSerialSend) {
        return required(request, "file send is a batch serial write operation");
    }
    if (request.kind == DangerousOperationKind::TimedSerialWrite) {
        return required(request, "timed send can repeat writes without further user action");
    }
    if (request.kind == DangerousOperationKind::CommandSequenceWrite) {
        return required(request, "command sequence can perform multiple write operations");
    }
    if (request.slaveId == 0 && isModbusWriteFunction(request.functionCode)) {
        DangerousOperationRequest classified = request;
        classified.kind = DangerousOperationKind::ModbusBroadcastWrite;
        return required(classified, "broadcast Modbus write can affect every device on the bus");
    }
    if (request.kind == DangerousOperationKind::ModbusBroadcastWrite) {
        return required(request, "broadcast Modbus write can affect every device on the bus");
    }
    if (request.kind == DangerousOperationKind::ModbusRegisterWrite || isDangerousRegisterWriteFunction(request.functionCode)) {
        DangerousOperationRequest classified = request;
        classified.kind = DangerousOperationKind::ModbusRegisterWrite;
        return required(classified, "Modbus register write can change device state");
    }
    return safe(request);
}

DangerousOperationAuditEvent makeDangerousOperationAuditEvent(
    const DangerousOperationPolicyResult& policy,
    DangerousOperationKind kind,
    DangerousOperationConfirmationState state) {
    DangerousOperationAuditEvent event;
    event.kind = policy.summary.empty() && policy.metadata.empty() ? kind : policy.kind;
    event.state = state;
    event.summary = policy.summary;
    event.metadata = policy.metadata;
    event.metadata["confirmation_result"] = dangerousOperationConfirmationStateName(state);
    return event;
}

} // namespace svm::core
