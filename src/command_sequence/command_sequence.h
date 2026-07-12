#pragma once

#include "core/byte_buffer.h"
#include "core/modbus_scan_executor_core.h"
#include "core/text.h"
#include "transport/serial_write_queue.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace svm::command_sequence {

enum class CommandKind {
    SerialWrite,
    Delay,
    WaitForResponse,
    ModbusRead,
    Assertion,
};

enum class AssertionKind {
    LastSerialWriteAccepted,
    LastResponseContains,
    LastModbusReadSucceeded,
    RegisterEquals,
};

enum class CommandExecutionStatus {
    Completed,
    Failed,
    Timeout,
    Cancelled,
    AssertionFailed,
    RejectedUnsafe,
};

enum class CommandEvidenceEventType {
    StepResult,
    AssertionResult,
};

struct SerialWriteCommand {
    core::ByteBuffer payload;
    int timeoutMs = transport::kDefaultSerialWriteTimeoutMs;
};

struct DelayCommand {
    int durationMs = 0;
};

struct WaitForResponseCommand {
    core::ByteBuffer expectedPayload;
    int timeoutMs = 1000;
};

struct ModbusReadCommand {
    int slaveId = 1;
    core::Byte functionCode = static_cast<core::Byte>(core::modbus::ModbusReadFunction::HoldingRegisters);
    int startAddress = 0;
    int quantity = 1;
    int responseTimeoutMs = 1000;
    int retryCount = 0;
};

struct AssertionCommand {
    AssertionKind kind = AssertionKind::LastSerialWriteAccepted;
    core::ByteBuffer expectedPayload;
    int registerAddress = 0;
    std::uint16_t expectedRegisterValue = 0;
};

using CommandAction = std::variant<
    SerialWriteCommand,
    DelayCommand,
    WaitForResponseCommand,
    ModbusReadCommand,
    AssertionCommand>;

struct CommandStep {
    core::Text id;
    CommandAction action;
};

struct CommandSequence {
    core::Text id;
    std::vector<CommandStep> steps;
};

struct CommandSequenceSafetyLimits {
    std::size_t maxCommandCount = 64;
    std::size_t maxPayloadBytes = 4096;
    int maxCommandTimeoutMs = 30000;
    int maxDelayMs = 60000;
    int maxTotalBudgetMs = 300000;
    int maxModbusQuantity = 125;
    int maxModbusRetries = 3;
};

struct CommandSequenceValidationResult {
    bool ok = false;
    CommandExecutionStatus status = CommandExecutionStatus::RejectedUnsafe;
    std::size_t stepIndex = 0;
    core::Text message;
};

struct CommandStepExecutionResult {
    std::size_t stepIndex = 0;
    core::Text stepId;
    CommandKind kind = CommandKind::SerialWrite;
    CommandExecutionStatus status = CommandExecutionStatus::Failed;
    core::Text message;
    std::map<core::Text, core::Text> metadata;
};

struct CommandSequenceEvidenceEvent {
    CommandEvidenceEventType type = CommandEvidenceEventType::StepResult;
    core::Text sequenceId;
    std::size_t stepIndex = 0;
    core::Text stepId;
    CommandKind commandKind = CommandKind::SerialWrite;
    CommandExecutionStatus status = CommandExecutionStatus::Failed;
    core::Text message;
    std::map<core::Text, core::Text> metadata;
};

struct CommandSequenceExecutionResult {
    CommandExecutionStatus status = CommandExecutionStatus::Failed;
    core::Text message;
    std::vector<CommandStepExecutionResult> steps;
    std::vector<CommandSequenceEvidenceEvent> evidence;

    [[nodiscard]] bool success() const noexcept;
};

struct CommandSequenceExecutionContext {
    transport::SerialWritePort* serialWriteTransport = nullptr;
    core::modbus::RtuTransport* modbusTransport = nullptr;
    std::function<std::optional<core::ByteBuffer>(int timeoutMs)> waitForResponse;
    std::function<void(int durationMs)> sleepForMs;
    std::function<bool()> shouldCancel;
    std::function<core::Text()> nowUtc;
};

CommandStep makeSerialWriteStep(core::Text id, core::ByteBuffer payload, int timeoutMs = transport::kDefaultSerialWriteTimeoutMs);
CommandStep makeDelayStep(core::Text id, int durationMs);
CommandStep makeWaitForResponseStep(core::Text id, core::ByteBuffer expectedPayload, int timeoutMs);
CommandStep makeModbusReadStep(core::Text id, ModbusReadCommand command);
CommandStep makeAssertionStep(core::Text id, AssertionCommand command);

[[nodiscard]] CommandKind commandKind(const CommandStep& step) noexcept;
[[nodiscard]] const char* commandKindName(CommandKind kind) noexcept;
[[nodiscard]] const char* assertionKindName(AssertionKind kind) noexcept;
[[nodiscard]] const char* commandExecutionStatusName(CommandExecutionStatus status) noexcept;
[[nodiscard]] bool isTerminalCommandExecutionStatus(CommandExecutionStatus status) noexcept;

[[nodiscard]] CommandSequenceValidationResult validateCommandSequence(
    const CommandSequence& sequence,
    const CommandSequenceSafetyLimits& limits = {});

[[nodiscard]] CommandSequenceExecutionResult executeCommandSequence(
    const CommandSequence& sequence,
    CommandSequenceExecutionContext context,
    const CommandSequenceSafetyLimits& limits = {});

} // namespace svm::command_sequence
