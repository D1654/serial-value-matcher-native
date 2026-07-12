#include "command_sequence/command_sequence.h"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace svm::command_sequence {
namespace {

using core::ByteBuffer;
using core::Text;

Text stepPath(std::size_t index) {
    return "step" + std::to_string(index + 1);
}

Text stepLabel(const CommandStep& step, std::size_t index) {
    if (!step.id.empty()) {
        return step.id;
    }
    return stepPath(index);
}

bool cancelled(const CommandSequenceExecutionContext& context) {
    return context.shouldCancel && context.shouldCancel();
}

Text toText(std::size_t value) {
    return std::to_string(value);
}

Text toText(int value) {
    return std::to_string(value);
}

Text boolText(bool value) {
    return value ? "true" : "false";
}

bool containsSubsequence(const ByteBuffer& haystack, const ByteBuffer& needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end()) != haystack.end();
}

bool addBudget(int& budgetMs, int valueMs) {
    if (valueMs <= 0) {
        return true;
    }
    if (budgetMs > std::numeric_limits<int>::max() - valueMs) {
        return false;
    }
    budgetMs += valueMs;
    return true;
}

CommandSequenceValidationResult valid() {
    return {
        .ok = true,
        .status = CommandExecutionStatus::Completed,
    };
}

CommandSequenceValidationResult unsafe(std::size_t stepIndex, Text message) {
    return {
        .ok = false,
        .status = CommandExecutionStatus::RejectedUnsafe,
        .stepIndex = stepIndex,
        .message = std::move(message),
    };
}

bool validCommandTimeout(int timeoutMs, const CommandSequenceSafetyLimits& limits) {
    return timeoutMs > 0 && timeoutMs <= limits.maxCommandTimeoutMs;
}

CommandSequenceValidationResult validateSerialWrite(
    const SerialWriteCommand& command,
    std::size_t stepIndex,
    const CommandSequenceSafetyLimits& limits,
    int& totalBudgetMs) {
    if (command.payload.empty()) {
        return unsafe(stepIndex, "Serial write command payload must not be empty.");
    }
    if (command.payload.size() > limits.maxPayloadBytes) {
        return unsafe(stepIndex, "Serial write command payload exceeds the configured safety limit.");
    }
    if (!validCommandTimeout(command.timeoutMs, limits)) {
        return unsafe(stepIndex, "Serial write command timeout is outside the configured safety limit.");
    }
    if (!addBudget(totalBudgetMs, command.timeoutMs)) {
        return unsafe(stepIndex, "Command sequence timeout budget overflowed.");
    }
    return valid();
}

CommandSequenceValidationResult validateDelay(
    const DelayCommand& command,
    std::size_t stepIndex,
    const CommandSequenceSafetyLimits& limits,
    int& totalBudgetMs) {
    if (command.durationMs < 0 || command.durationMs > limits.maxDelayMs) {
        return unsafe(stepIndex, "Delay command duration is outside the configured safety limit.");
    }
    if (!addBudget(totalBudgetMs, command.durationMs)) {
        return unsafe(stepIndex, "Command sequence timeout budget overflowed.");
    }
    return valid();
}

CommandSequenceValidationResult validateWaitForResponse(
    const WaitForResponseCommand& command,
    std::size_t stepIndex,
    const CommandSequenceSafetyLimits& limits,
    int& totalBudgetMs) {
    if (command.expectedPayload.empty()) {
        return unsafe(stepIndex, "Wait-for-response command must declare the expected payload bytes.");
    }
    if (command.expectedPayload.size() > limits.maxPayloadBytes) {
        return unsafe(stepIndex, "Wait-for-response payload exceeds the configured safety limit.");
    }
    if (!validCommandTimeout(command.timeoutMs, limits)) {
        return unsafe(stepIndex, "Wait-for-response timeout is outside the configured safety limit.");
    }
    if (!addBudget(totalBudgetMs, command.timeoutMs)) {
        return unsafe(stepIndex, "Command sequence timeout budget overflowed.");
    }
    return valid();
}

CommandSequenceValidationResult validateModbusRead(
    const ModbusReadCommand& command,
    std::size_t stepIndex,
    const CommandSequenceSafetyLimits& limits,
    int& totalBudgetMs) {
    if (command.slaveId < 1 || command.slaveId > 247) {
        return unsafe(stepIndex, "Modbus read command slave id must be 1-247; broadcast id 0 is not allowed.");
    }
    if (!core::modbus::isSupportedReadFunction(command.functionCode)) {
        return unsafe(stepIndex, "Modbus read command only supports read functions FC03 and FC04.");
    }
    if (command.startAddress < 0) {
        return unsafe(stepIndex, "Modbus read command start address must not be negative.");
    }
    if (command.quantity <= 0 || command.quantity > limits.maxModbusQuantity) {
        return unsafe(stepIndex, "Modbus read command quantity is outside the configured safety limit.");
    }
    if (command.retryCount < 0 || command.retryCount > limits.maxModbusRetries) {
        return unsafe(stepIndex, "Modbus read command retry count is outside the configured safety limit.");
    }
    if (!validCommandTimeout(command.responseTimeoutMs, limits)) {
        return unsafe(stepIndex, "Modbus read command response timeout is outside the configured safety limit.");
    }
    const int attemptCount = command.retryCount + 1;
    if (attemptCount > 0 && command.responseTimeoutMs > std::numeric_limits<int>::max() / attemptCount) {
        return unsafe(stepIndex, "Command sequence timeout budget overflowed.");
    }
    if (!addBudget(totalBudgetMs, command.responseTimeoutMs * attemptCount)) {
        return unsafe(stepIndex, "Command sequence timeout budget overflowed.");
    }
    return valid();
}

CommandSequenceValidationResult validateAssertion(
    const AssertionCommand& command,
    std::size_t stepIndex,
    const CommandSequenceSafetyLimits& limits) {
    switch (command.kind) {
    case AssertionKind::LastSerialWriteAccepted:
    case AssertionKind::LastModbusReadSucceeded:
        return valid();
    case AssertionKind::LastResponseContains:
        if (command.expectedPayload.empty()) {
            return unsafe(stepIndex, "Response assertion must declare expected payload bytes.");
        }
        if (command.expectedPayload.size() > limits.maxPayloadBytes) {
            return unsafe(stepIndex, "Response assertion payload exceeds the configured safety limit.");
        }
        return valid();
    case AssertionKind::RegisterEquals:
        if (command.registerAddress < 0) {
            return unsafe(stepIndex, "Register assertion address must not be negative.");
        }
        return valid();
    }
    return unsafe(stepIndex, "Unknown assertion command kind.");
}

CommandSequenceValidationResult validateStep(
    const CommandStep& step,
    std::size_t stepIndex,
    const CommandSequenceSafetyLimits& limits,
    int& totalBudgetMs) {
    return std::visit(
        [&](const auto& command) -> CommandSequenceValidationResult {
            using Command = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<Command, SerialWriteCommand>) {
                return validateSerialWrite(command, stepIndex, limits, totalBudgetMs);
            } else if constexpr (std::is_same_v<Command, DelayCommand>) {
                return validateDelay(command, stepIndex, limits, totalBudgetMs);
            } else if constexpr (std::is_same_v<Command, WaitForResponseCommand>) {
                return validateWaitForResponse(command, stepIndex, limits, totalBudgetMs);
            } else if constexpr (std::is_same_v<Command, ModbusReadCommand>) {
                return validateModbusRead(command, stepIndex, limits, totalBudgetMs);
            } else {
                return validateAssertion(command, stepIndex, limits);
            }
        },
        step.action);
}

CommandStepExecutionResult makeStepResult(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    CommandExecutionStatus status,
    Text message) {
    (void)sequence;
    return {
        .stepIndex = stepIndex,
        .stepId = stepLabel(step, stepIndex),
        .kind = commandKind(step),
        .status = status,
        .message = std::move(message),
    };
}

void appendEvidence(
    const CommandSequence& sequence,
    const CommandStepExecutionResult& stepResult,
    CommandEvidenceEventType type,
    std::vector<CommandSequenceEvidenceEvent>& evidence) {
    evidence.push_back(CommandSequenceEvidenceEvent{
        .type = type,
        .sequenceId = sequence.id,
        .stepIndex = stepResult.stepIndex,
        .stepId = stepResult.stepId,
        .commandKind = stepResult.kind,
        .status = stepResult.status,
        .message = stepResult.message,
        .metadata = stepResult.metadata,
    });
}

CommandStepExecutionResult runSerialWrite(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    const SerialWriteCommand& command,
    CommandSequenceExecutionContext& context,
    std::optional<transport::SerialWriteResult>& lastSerialWriteResult) {
    auto result = makeStepResult(sequence, step, stepIndex, CommandExecutionStatus::Failed, {});
    result.metadata["payload_bytes"] = toText(command.payload.size());
    result.metadata["timeout_ms"] = toText(command.timeoutMs);
    result.metadata["backend"] = "serial_write_transport";

    if (!context.serialWriteTransport) {
        result.message = "Serial write transport backend is not available.";
        return result;
    }

    auto writeResult = context.serialWriteTransport->enqueueWrite(command.payload, command.timeoutMs);
    lastSerialWriteResult = writeResult;
    result.metadata["serial_request_id"] = toText(writeResult.requestId);
    result.metadata["serial_status"] = transport::serialWriteResultStatusName(writeResult.status);
    result.metadata["byte_count"] = toText(writeResult.byteCount);
    result.status = writeResult.accepted() ? CommandExecutionStatus::Completed : CommandExecutionStatus::Failed;
    result.message = std::move(writeResult.message);
    return result;
}

CommandStepExecutionResult runDelay(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    const DelayCommand& command,
    CommandSequenceExecutionContext& context) {
    auto result = makeStepResult(sequence, step, stepIndex, CommandExecutionStatus::Completed, {});
    result.metadata["duration_ms"] = toText(command.durationMs);
    if (context.sleepForMs && command.durationMs > 0) {
        context.sleepForMs(command.durationMs);
    }
    return result;
}

CommandStepExecutionResult runWaitForResponse(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    const WaitForResponseCommand& command,
    CommandSequenceExecutionContext& context,
    std::optional<ByteBuffer>& lastResponse) {
    auto result = makeStepResult(sequence, step, stepIndex, CommandExecutionStatus::Failed, {});
    result.metadata["expected_payload_bytes"] = toText(command.expectedPayload.size());
    result.metadata["timeout_ms"] = toText(command.timeoutMs);

    if (!context.waitForResponse) {
        result.message = "Wait-for-response backend is not available.";
        return result;
    }

    const std::optional<ByteBuffer> response = context.waitForResponse(command.timeoutMs);
    if (!response.has_value()) {
        result.status = CommandExecutionStatus::Timeout;
        result.message = "Waiting for response timed out.";
        return result;
    }

    lastResponse = *response;
    result.metadata["response_payload_bytes"] = toText(response->size());
    if (!containsSubsequence(*response, command.expectedPayload)) {
        result.message = "Response did not contain the declared expected payload.";
        return result;
    }

    result.status = CommandExecutionStatus::Completed;
    return result;
}

core::modbus::ScanExecutionOptions modbusExecutionOptions(
    const ModbusReadCommand& command,
    const CommandSequenceExecutionContext& context) {
    core::modbus::ScanExecutionOptions options;
    options.responseTimeoutMs = command.responseTimeoutMs;
    options.continueOnBlockError = false;
    options.retryOnTimeout = true;
    options.retryOnTransportError = true;
    options.retryOnParseError = false;
    options.retryOnModbusException = false;
    options.shouldCancel = context.shouldCancel;
    options.nowUtc = context.nowUtc;
    return options;
}

CommandStepExecutionResult runModbusRead(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    const ModbusReadCommand& command,
    CommandSequenceExecutionContext& context,
    std::optional<core::modbus::ScanExecutionResult>& lastModbusResult) {
    auto result = makeStepResult(sequence, step, stepIndex, CommandExecutionStatus::Failed, {});
    result.metadata["backend"] = "core_modbus_scan_executor";
    result.metadata["slave_id"] = toText(command.slaveId);
    result.metadata["function_code"] = toText(static_cast<int>(command.functionCode));
    result.metadata["start_address"] = toText(command.startAddress);
    result.metadata["quantity"] = toText(command.quantity);
    result.metadata["timeout_ms"] = toText(command.responseTimeoutMs);
    result.metadata["retry_count"] = toText(command.retryCount);

    if (!context.modbusTransport) {
        result.message = "Modbus transport backend is not available.";
        return result;
    }

    core::modbus::ScanPlanOptions planOptions;
    planOptions.slaveId = command.slaveId;
    planOptions.functionCode = command.functionCode;
    planOptions.range = {
        .startAddress = command.startAddress,
        .endAddress = command.startAddress + command.quantity - 1,
    };
    planOptions.blockSize = command.quantity;
    planOptions.requestIntervalMs = 0;
    planOptions.retryCount = command.retryCount;
    const auto plan = core::modbus::buildScanPlan(planOptions);
    if (!plan.ok) {
        result.message = plan.errorMessage;
        return result;
    }

    core::modbus::ScanExecutor executor(*context.modbusTransport);
    auto scanResult = executor.execute(plan.plan, modbusExecutionOptions(command, context));
    result.metadata["modbus_status"] = core::modbus::describeScanExecutionStatus(scanResult.status);
    result.metadata["observation_count"] = toText(scanResult.observations.size());
    result.metadata["success_block_count"] = toText(scanResult.successBlockCount);
    result.metadata["failed_block_count"] = toText(scanResult.failedBlockCount);
    result.message = scanResult.errorMessage;
    lastModbusResult = std::move(scanResult);

    const auto status = lastModbusResult->status;
    if (status == core::modbus::ScanExecutionStatus::Completed) {
        result.status = CommandExecutionStatus::Completed;
        result.message.clear();
    } else if (lastModbusResult->errorMessage == "Scan was cancelled.") {
        result.status = CommandExecutionStatus::Cancelled;
    } else if (!lastModbusResult->blocks.empty()
        && !lastModbusResult->blocks.front().attempts.empty()
        && lastModbusResult->blocks.front().attempts.front().status == core::modbus::ScanAttemptStatus::Timeout) {
        result.status = CommandExecutionStatus::Timeout;
    }
    return result;
}

bool registerEquals(const core::modbus::ScanExecutionResult& result, int address, std::uint16_t value) {
    return std::any_of(result.observations.begin(), result.observations.end(), [address, value](const auto& observation) {
        return observation.address == address && observation.value == value;
    });
}

CommandStepExecutionResult runAssertion(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    const AssertionCommand& command,
    const std::optional<transport::SerialWriteResult>& lastSerialWriteResult,
    const std::optional<ByteBuffer>& lastResponse,
    const std::optional<core::modbus::ScanExecutionResult>& lastModbusResult) {
    auto result = makeStepResult(sequence, step, stepIndex, CommandExecutionStatus::Completed, {});
    result.metadata["assertion_kind"] = assertionKindName(command.kind);
    bool passed = false;

    switch (command.kind) {
    case AssertionKind::LastSerialWriteAccepted:
        passed = lastSerialWriteResult.has_value() && lastSerialWriteResult->accepted();
        break;
    case AssertionKind::LastResponseContains:
        result.metadata["expected_payload_bytes"] = toText(command.expectedPayload.size());
        passed = lastResponse.has_value() && containsSubsequence(*lastResponse, command.expectedPayload);
        break;
    case AssertionKind::LastModbusReadSucceeded:
        passed = lastModbusResult.has_value()
            && lastModbusResult->status == core::modbus::ScanExecutionStatus::Completed;
        break;
    case AssertionKind::RegisterEquals:
        result.metadata["register_address"] = toText(command.registerAddress);
        result.metadata["expected_register_value"] = toText(static_cast<int>(command.expectedRegisterValue));
        passed = lastModbusResult.has_value()
            && registerEquals(*lastModbusResult, command.registerAddress, command.expectedRegisterValue);
        break;
    }

    result.metadata["passed"] = boolText(passed);
    if (!passed) {
        result.status = CommandExecutionStatus::AssertionFailed;
        result.message = "Command sequence assertion failed.";
    }
    return result;
}

CommandStepExecutionResult runStep(
    const CommandSequence& sequence,
    const CommandStep& step,
    std::size_t stepIndex,
    CommandSequenceExecutionContext& context,
    std::optional<transport::SerialWriteResult>& lastSerialWriteResult,
    std::optional<ByteBuffer>& lastResponse,
    std::optional<core::modbus::ScanExecutionResult>& lastModbusResult) {
    return std::visit(
        [&](const auto& command) -> CommandStepExecutionResult {
            using Command = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<Command, SerialWriteCommand>) {
                return runSerialWrite(sequence, step, stepIndex, command, context, lastSerialWriteResult);
            } else if constexpr (std::is_same_v<Command, DelayCommand>) {
                return runDelay(sequence, step, stepIndex, command, context);
            } else if constexpr (std::is_same_v<Command, WaitForResponseCommand>) {
                return runWaitForResponse(sequence, step, stepIndex, command, context, lastResponse);
            } else if constexpr (std::is_same_v<Command, ModbusReadCommand>) {
                return runModbusRead(sequence, step, stepIndex, command, context, lastModbusResult);
            } else {
                return runAssertion(sequence, step, stepIndex, command, lastSerialWriteResult, lastResponse, lastModbusResult);
            }
        },
        step.action);
}

} // namespace

bool CommandSequenceExecutionResult::success() const noexcept {
    return status == CommandExecutionStatus::Completed;
}

CommandStep makeSerialWriteStep(Text id, ByteBuffer payload, int timeoutMs) {
    return CommandStep{
        .id = std::move(id),
        .action = SerialWriteCommand{std::move(payload), timeoutMs},
    };
}

CommandStep makeDelayStep(Text id, int durationMs) {
    return CommandStep{
        .id = std::move(id),
        .action = DelayCommand{durationMs},
    };
}

CommandStep makeWaitForResponseStep(Text id, ByteBuffer expectedPayload, int timeoutMs) {
    return CommandStep{
        .id = std::move(id),
        .action = WaitForResponseCommand{std::move(expectedPayload), timeoutMs},
    };
}

CommandStep makeModbusReadStep(Text id, ModbusReadCommand command) {
    return CommandStep{
        .id = std::move(id),
        .action = command,
    };
}

CommandStep makeAssertionStep(Text id, AssertionCommand command) {
    return CommandStep{
        .id = std::move(id),
        .action = command,
    };
}

CommandKind commandKind(const CommandStep& step) noexcept {
    return std::visit(
        [](const auto& command) -> CommandKind {
            using Command = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<Command, SerialWriteCommand>) {
                return CommandKind::SerialWrite;
            } else if constexpr (std::is_same_v<Command, DelayCommand>) {
                return CommandKind::Delay;
            } else if constexpr (std::is_same_v<Command, WaitForResponseCommand>) {
                return CommandKind::WaitForResponse;
            } else if constexpr (std::is_same_v<Command, ModbusReadCommand>) {
                return CommandKind::ModbusRead;
            } else {
                return CommandKind::Assertion;
            }
        },
        step.action);
}

const char* commandKindName(CommandKind kind) noexcept {
    switch (kind) {
    case CommandKind::SerialWrite:
        return "serial_write";
    case CommandKind::Delay:
        return "delay";
    case CommandKind::WaitForResponse:
        return "wait_for_response";
    case CommandKind::ModbusRead:
        return "modbus_read";
    case CommandKind::Assertion:
        return "assertion";
    }
    return "unknown";
}

const char* assertionKindName(AssertionKind kind) noexcept {
    switch (kind) {
    case AssertionKind::LastSerialWriteAccepted:
        return "last_serial_write_accepted";
    case AssertionKind::LastResponseContains:
        return "last_response_contains";
    case AssertionKind::LastModbusReadSucceeded:
        return "last_modbus_read_succeeded";
    case AssertionKind::RegisterEquals:
        return "register_equals";
    }
    return "unknown";
}

const char* commandExecutionStatusName(CommandExecutionStatus status) noexcept {
    switch (status) {
    case CommandExecutionStatus::Completed:
        return "completed";
    case CommandExecutionStatus::Failed:
        return "failed";
    case CommandExecutionStatus::Timeout:
        return "timeout";
    case CommandExecutionStatus::Cancelled:
        return "cancelled";
    case CommandExecutionStatus::AssertionFailed:
        return "assertion_failed";
    case CommandExecutionStatus::RejectedUnsafe:
        return "rejected_unsafe";
    }
    return "unknown";
}

bool isTerminalCommandExecutionStatus(CommandExecutionStatus status) noexcept {
    switch (status) {
    case CommandExecutionStatus::Completed:
    case CommandExecutionStatus::Failed:
    case CommandExecutionStatus::Timeout:
    case CommandExecutionStatus::Cancelled:
    case CommandExecutionStatus::AssertionFailed:
    case CommandExecutionStatus::RejectedUnsafe:
        return true;
    }
    return false;
}

CommandSequenceValidationResult validateCommandSequence(
    const CommandSequence& sequence,
    const CommandSequenceSafetyLimits& limits) {
    if (sequence.steps.empty()) {
        return unsafe(0, "Command sequence must contain at least one command.");
    }
    if (limits.maxCommandCount == 0 || sequence.steps.size() > limits.maxCommandCount) {
        return unsafe(0, "Command sequence exceeds the configured command-count safety limit.");
    }
    if (limits.maxPayloadBytes == 0 || limits.maxCommandTimeoutMs <= 0 || limits.maxDelayMs < 0
        || limits.maxTotalBudgetMs <= 0 || limits.maxModbusQuantity <= 0 || limits.maxModbusRetries < 0) {
        return unsafe(0, "Command sequence safety limits are invalid.");
    }

    int totalBudgetMs = 0;
    for (std::size_t index = 0; index < sequence.steps.size(); ++index) {
        const auto stepResult = validateStep(sequence.steps[index], index, limits, totalBudgetMs);
        if (!stepResult.ok) {
            return stepResult;
        }
        if (totalBudgetMs > limits.maxTotalBudgetMs) {
            return unsafe(index, "Command sequence exceeds the configured total timeout budget.");
        }
    }

    return valid();
}

CommandSequenceExecutionResult executeCommandSequence(
    const CommandSequence& sequence,
    CommandSequenceExecutionContext context,
    const CommandSequenceSafetyLimits& limits) {
    CommandSequenceExecutionResult result;
    const auto validation = validateCommandSequence(sequence, limits);
    if (!validation.ok) {
        result.status = validation.status;
        result.message = validation.message;
        return result;
    }

    std::optional<transport::SerialWriteResult> lastSerialWriteResult;
    std::optional<ByteBuffer> lastResponse;
    std::optional<core::modbus::ScanExecutionResult> lastModbusResult;

    for (std::size_t index = 0; index < sequence.steps.size(); ++index) {
        const CommandStep& step = sequence.steps[index];
        if (cancelled(context)) {
            auto cancelledStep = makeStepResult(sequence, step, index, CommandExecutionStatus::Cancelled, "Command sequence was cancelled.");
            appendEvidence(sequence, cancelledStep, CommandEvidenceEventType::StepResult, result.evidence);
            result.steps.push_back(std::move(cancelledStep));
            result.status = CommandExecutionStatus::Cancelled;
            result.message = "Command sequence was cancelled.";
            return result;
        }

        auto stepResult = runStep(sequence, step, index, context, lastSerialWriteResult, lastResponse, lastModbusResult);
        const auto evidenceType = commandKind(step) == CommandKind::Assertion
            ? CommandEvidenceEventType::AssertionResult
            : CommandEvidenceEventType::StepResult;
        appendEvidence(sequence, stepResult, evidenceType, result.evidence);
        result.steps.push_back(std::move(stepResult));

        if (result.steps.back().status != CommandExecutionStatus::Completed) {
            result.status = result.steps.back().status;
            result.message = result.steps.back().message;
            return result;
        }
    }

    result.status = CommandExecutionStatus::Completed;
    return result;
}

} // namespace svm::command_sequence
