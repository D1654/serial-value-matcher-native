#pragma once

#include "core/byte_buffer.h"
#include "core/text.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace svm::core {

enum class DangerousOperationKind {
    ManualSerialWrite,
    TimedSerialWrite,
    FileSerialSend,
    CommandSequenceWrite,
    ModbusBroadcastWrite,
    ModbusRegisterWrite,
};

enum class DangerousOperationConfirmationState {
    NotRequired,
    Required,
    Confirmed,
    Cancelled,
    PromptFailed,
};

struct DangerousOperationRequest {
    DangerousOperationKind kind = DangerousOperationKind::ManualSerialWrite;
    std::size_t payloadBytes = 0;
    std::size_t commandCount = 0;
    int repeatCount = 1;
    int slaveId = 1;
    Byte functionCode = 0;
    int address = 0;
    int quantity = 0;
};

struct DangerousOperationPolicyResult {
    DangerousOperationKind kind = DangerousOperationKind::ManualSerialWrite;
    bool requiresConfirmation = false;
    Text reason;
    Text summary;
    std::map<Text, Text> metadata;
};

struct DangerousOperationAuditEvent {
    DangerousOperationKind kind = DangerousOperationKind::ManualSerialWrite;
    DangerousOperationConfirmationState state = DangerousOperationConfirmationState::NotRequired;
    Text summary;
    std::map<Text, Text> metadata;
};

[[nodiscard]] const char* dangerousOperationKindName(DangerousOperationKind kind) noexcept;
[[nodiscard]] const char* dangerousOperationConfirmationStateName(DangerousOperationConfirmationState state) noexcept;

[[nodiscard]] bool isModbusWriteFunction(Byte functionCode) noexcept;
[[nodiscard]] bool isDangerousRegisterWriteFunction(Byte functionCode) noexcept;
[[nodiscard]] bool dangerousOperationConfirmationSatisfied(DangerousOperationConfirmationState state) noexcept;
[[nodiscard]] DangerousOperationConfirmationState dangerousOperationConfirmationStateFromDialogResult(int dialogResult) noexcept;

[[nodiscard]] DangerousOperationPolicyResult evaluateDangerousOperation(const DangerousOperationRequest& request);
[[nodiscard]] DangerousOperationAuditEvent makeDangerousOperationAuditEvent(
    const DangerousOperationPolicyResult& policy,
    DangerousOperationKind kind,
    DangerousOperationConfirmationState state);

} // namespace svm::core
