#include "command_sequence/command_sequence.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using svm::command_sequence::AssertionCommand;
using svm::command_sequence::AssertionKind;
using svm::command_sequence::CommandEvidenceEventType;
using svm::command_sequence::CommandExecutionStatus;
using svm::command_sequence::CommandSequence;
using svm::command_sequence::CommandSequenceExecutionContext;
using svm::command_sequence::ModbusReadCommand;
using svm::core::Byte;
using svm::core::ByteBuffer;

ByteBuffer modbusReadResponse(int slaveId, Byte functionCode, int startAddress, int quantity) {
    ByteBuffer body;
    body.push_back(static_cast<Byte>(slaveId));
    body.push_back(functionCode);
    body.push_back(static_cast<Byte>(quantity * 2));
    for (int offset = 0; offset < quantity; ++offset) {
        const auto value = static_cast<std::uint16_t>(startAddress + offset == 11 ? 42 : 100 + offset);
        body.push_back(static_cast<Byte>((value >> 8) & 0xFF));
        body.push_back(static_cast<Byte>(value & 0xFF));
    }
    return svm::core::modbus::appendCrc16Modbus(body);
}

class FakeModbusTransport final : public svm::core::modbus::RtuTransport {
public:
    struct ScriptedResult {
        svm::core::modbus::RtuTransportExchangeStatus status;
        svm::core::Text errorMessage;
    };

    void enqueueResult(
        svm::core::modbus::RtuTransportExchangeStatus status,
        svm::core::Text errorMessage) {
        scriptedResults.push_back({status, std::move(errorMessage)});
    }

    svm::core::modbus::RtuTransportExchange exchange(svm::core::ByteSpan requestFrame, int responseTimeoutMs) override {
        timeoutValues.push_back(responseTimeoutMs);
        requests.emplace_back(requestFrame.begin(), requestFrame.end());

        svm::core::modbus::RtuTransportExchange result;
        result.status = svm::core::modbus::RtuTransportExchangeStatus::Success;
        result.requestFrame = requests.back();
        result.sentAtUtc = "2026-07-09T09:00:00Z";
        result.receivedAtUtc = "2026-07-09T09:00:00Z";
        result.endpoint = "fake://command-sequence-modbus";

        if (requestFrame.size() < 6) {
            result.status = svm::core::modbus::RtuTransportExchangeStatus::TransportError;
            result.errorMessage = "bad request";
            if (onExchange) {
                onExchange();
            }
            return result;
        }

        if (nextScriptedResult < scriptedResults.size()) {
            const ScriptedResult& scripted = scriptedResults[nextScriptedResult++];
            result.status = scripted.status;
            result.errorMessage = scripted.errorMessage;
            if (scripted.status != svm::core::modbus::RtuTransportExchangeStatus::Success) {
                if (onExchange) {
                    onExchange();
                }
                return result;
            }
        }

        const int slaveId = requestFrame[0];
        const Byte functionCode = requestFrame[1];
        const int startAddress = (static_cast<int>(requestFrame[2]) << 8) | static_cast<int>(requestFrame[3]);
        const int quantity = (static_cast<int>(requestFrame[4]) << 8) | static_cast<int>(requestFrame[5]);
        result.responseFrame = modbusReadResponse(slaveId, functionCode, startAddress, quantity);
        if (onExchange) {
            onExchange();
        }
        return result;
    }

    std::vector<ByteBuffer> requests;
    std::vector<int> timeoutValues;
    std::vector<ScriptedResult> scriptedResults;
    std::size_t nextScriptedResult = 0;
    std::function<void()> onExchange;
};

AssertionCommand serialAcceptedAssertion() {
    AssertionCommand command;
    command.kind = AssertionKind::LastSerialWriteAccepted;
    return command;
}

AssertionCommand responseContainsAssertion(ByteBuffer payload) {
    AssertionCommand command;
    command.kind = AssertionKind::LastResponseContains;
    command.expectedPayload = std::move(payload);
    return command;
}

AssertionCommand registerEqualsAssertion(int address, std::uint16_t value) {
    AssertionCommand command;
    command.kind = AssertionKind::RegisterEquals;
    command.registerAddress = address;
    command.expectedRegisterValue = value;
    return command;
}

ModbusReadCommand readRegisters(int startAddress, int quantity) {
    ModbusReadCommand command;
    command.slaveId = 1;
    command.functionCode = static_cast<Byte>(svm::core::modbus::ModbusReadFunction::HoldingRegisters);
    command.startAddress = startAddress;
    command.quantity = quantity;
    command.responseTimeoutMs = 250;
    return command;
}

CommandSequence modbusReadSequence(svm::core::Text id, int retryCount) {
    ModbusReadCommand command = readRegisters(10, 1);
    command.retryCount = retryCount;

    CommandSequence sequence;
    sequence.id = std::move(id);
    sequence.steps = {
        svm::command_sequence::makeModbusReadStep("read", command),
    };
    return sequence;
}

bool hasMetadataValue(
    const std::map<svm::core::Text, svm::core::Text>& metadata,
    const std::string& key,
    const std::string& expected) {
    const auto found = metadata.find(key);
    return found != metadata.end() && found->second == expected;
}

class FakeSerialWriteScheduler final : public svm::transport::SerialWriteScheduler {
public:
    struct PendingWrite {
        ByteBuffer payload;
        svm::transport::SerialDeadline deadline;
    };

    explicit FakeSerialWriteScheduler(std::size_t capacity)
        : capacity_(capacity) {
    }

    svm::transport::SerialWriteAdmissionResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline) override {
        if (payload.empty()) {
            return rejected(svm::transport::SerialOperationStatus::RejectedInvalid, svm::transport::SerialErrorCategory::InvalidInput);
        }
        if (pending_.size() >= capacity_) {
            return rejected(svm::transport::SerialOperationStatus::RejectedFull, svm::transport::SerialErrorCategory::QueueFull);
        }

        const svm::transport::SerialOperationId requestId = nextRequestId_++;
        const std::size_t byteCount = payload.size();
        pending_.push_back({std::move(payload), deadline});
        return admission(requestId, deadline, byteCount);
    }

    std::vector<svm::transport::SerialTerminalResult> cancelPendingWrites() override {
        std::vector<svm::transport::SerialTerminalResult> results;
        results.reserve(pending_.size());
        while (!pending_.empty()) {
            const std::size_t byteCount = pending_.front().payload.size();
            pending_.erase(pending_.begin());
            results.push_back({
                .operation = {
                    .requestId = nextRequestId_++,
                    .generation = generation_,
                    .kind = svm::transport::SerialOperationKind::Write,
                },
                .status = svm::transport::SerialOperationStatus::Cancelled,
                .byteCount = byteCount,
                .endpoint = "fake://command-sequence-serial",
                .error = {.category = svm::transport::SerialErrorCategory::Cancelled},
            });
        }
        return results;
    }

    std::vector<svm::transport::SerialTerminalResult> takeCompletedWrites() override {
        return {};
    }

    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override {
        return {
            .capacity = capacity_,
            .pendingCount = pending_.size(),
            .nextRequestId = nextRequestId_,
        };
    }

    [[nodiscard]] std::size_t pendingCount() const noexcept {
        return pending_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return pending_.empty();
    }

    [[nodiscard]] const PendingWrite& front() const {
        return pending_.front();
    }

private:
    svm::transport::SerialWriteAdmissionResult admission(
        svm::transport::SerialOperationId requestId,
        svm::transport::SerialDeadline deadline,
        std::size_t byteCount) const {
        return {
            .operation = {
                .requestId = requestId,
                .generation = generation_,
                .kind = svm::transport::SerialOperationKind::Write,
                .deadline = deadline,
            },
            .status = svm::transport::SerialOperationStatus::Accepted,
            .deadlineStatus = deadline.set()
                ? svm::transport::SerialDeadlineStatus::Pending
                : svm::transport::SerialDeadlineStatus::NotSet,
            .byteCount = byteCount,
            .endpoint = "fake://command-sequence-serial",
        };
    }

    svm::transport::SerialWriteAdmissionResult rejected(
        svm::transport::SerialOperationStatus status,
        svm::transport::SerialErrorCategory category) const {
        return {
            .operation = {
                .generation = generation_,
                .kind = svm::transport::SerialOperationKind::Write,
            },
            .status = status,
            .endpoint = "fake://command-sequence-serial",
            .error = {.category = category},
        };
    }

    std::size_t capacity_ = 0;
    std::vector<PendingWrite> pending_;
    svm::transport::SerialSessionGeneration generation_ = 9;
    svm::transport::SerialOperationId nextRequestId_ = 1;
};

void validSequenceUsesExistingBackendsAndRecordsEvidence() {
    FakeSerialWriteScheduler scheduler(4);
    FakeModbusTransport transport;
    int waitTimeoutMs = 0;
    int sleptMs = 0;

    CommandSequence sequence;
    sequence.id = "seq-valid";
    sequence.steps = {
        svm::command_sequence::makeSerialWriteStep("send", {0x10, 0x02}, 200),
        svm::command_sequence::makeDelayStep("settle", 5),
        svm::command_sequence::makeWaitForResponseStep("wait-ack", {0x06}, 300),
        svm::command_sequence::makeModbusReadStep("read", readRegisters(10, 2)),
        svm::command_sequence::makeAssertionStep("serial-ok", serialAcceptedAssertion()),
        svm::command_sequence::makeAssertionStep("ack-ok", responseContainsAssertion({0x06})),
        svm::command_sequence::makeAssertionStep("register-ok", registerEqualsAssertion(11, 42)),
    };

    CommandSequenceExecutionContext context;
    context.serialWriteScheduler = &scheduler;
    context.modbusTransport = &transport;
    context.waitForResponse = [&waitTimeoutMs](int timeoutMs) -> std::optional<ByteBuffer> {
        waitTimeoutMs = timeoutMs;
        return ByteBuffer{0x01, 0x06, 0x03};
    };
    context.sleepForMs = [&sleptMs](int durationMs) {
        sleptMs += durationMs;
    };
    context.nowUtc = [] {
        return svm::core::Text("2026-07-09T09:00:00Z");
    };

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);

    assert(result.status == CommandExecutionStatus::Completed);
    assert(result.success());
    assert(result.steps.size() == sequence.steps.size());
    assert(result.evidence.size() == sequence.steps.size());
    assert(result.evidence.back().type == CommandEvidenceEventType::AssertionResult);
    assert(hasMetadataValue(result.evidence.back().metadata, "passed", "true"));
    assert(hasMetadataValue(result.steps.front().metadata, "backend", "serial_write_scheduler"));
    assert(hasMetadataValue(result.steps.front().metadata, "serial_request_id", "1"));
    assert(hasMetadataValue(result.steps.front().metadata, "serial_generation", "9"));
    assert(hasMetadataValue(result.steps.front().metadata, "serial_status", "accepted"));
    assert(hasMetadataValue(result.steps.front().metadata, "serial_error_category", "none"));
    assert(hasMetadataValue(result.steps.front().metadata, "deadline_status", "pending"));
    assert(hasMetadataValue(result.steps.front().metadata, "deadline_set", "true"));
    assert(hasMetadataValue(result.steps.front().metadata, "byte_count", "2"));
    assert(scheduler.pendingCount() == 1);
    assert(scheduler.front().payload == ByteBuffer({0x10, 0x02}));
    assert(scheduler.front().deadline.set());
    assert(waitTimeoutMs == 300);
    assert(sleptMs == 5);
    assert(transport.requests.size() == 1);
    assert(transport.timeoutValues == std::vector<int>({250}));
}

void waitForResponseTimeoutStopsSequence() {
    bool delayRan = false;
    CommandSequence sequence;
    sequence.id = "seq-timeout";
    sequence.steps = {
        svm::command_sequence::makeWaitForResponseStep("wait", {0x06}, 100),
        svm::command_sequence::makeDelayStep("should-not-run", 10),
    };

    CommandSequenceExecutionContext context;
    context.waitForResponse = [](int) -> std::optional<ByteBuffer> {
        return std::nullopt;
    };
    context.sleepForMs = [&delayRan](int) {
        delayRan = true;
    };

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);

    assert(result.status == CommandExecutionStatus::Timeout);
    assert(result.steps.size() == 1);
    assert(result.evidence.size() == 1);
    assert(!delayRan);
}

void cancellationStopsBeforeDispatchingBackendWork() {
    FakeSerialWriteScheduler scheduler(4);
    CommandSequence sequence;
    sequence.id = "seq-cancel";
    sequence.steps = {
        svm::command_sequence::makeSerialWriteStep("send", {0x01}, 100),
    };

    CommandSequenceExecutionContext context;
    context.serialWriteScheduler = &scheduler;
    context.shouldCancel = [] {
        return true;
    };

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);

    assert(result.status == CommandExecutionStatus::Cancelled);
    assert(result.steps.size() == 1);
    assert(result.steps[0].status == CommandExecutionStatus::Cancelled);
    assert(scheduler.empty());
}

void modbusRetryClassificationUsesFinalTypedAttempt() {
    {
        FakeModbusTransport transport;
        transport.enqueueResult(
            svm::core::modbus::RtuTransportExchangeStatus::Timeout,
            "first timeout text is not a status");
        transport.enqueueResult(
            svm::core::modbus::RtuTransportExchangeStatus::TransportError,
            "final transport failure");

        CommandSequenceExecutionContext context;
        context.modbusTransport = &transport;
        const auto result = svm::command_sequence::executeCommandSequence(
            modbusReadSequence("seq-timeout-then-transport", 1),
            context);

        assert(result.status == CommandExecutionStatus::Failed);
        assert(result.steps.size() == 1);
        assert(result.steps[0].status == CommandExecutionStatus::Failed);
        assert(result.message == "final transport failure");
        assert(transport.requests.size() == 2);
    }

    {
        FakeModbusTransport transport;
        transport.enqueueResult(
            svm::core::modbus::RtuTransportExchangeStatus::TransportError,
            "first transport failure");
        transport.enqueueResult(
            svm::core::modbus::RtuTransportExchangeStatus::Timeout,
            "final timeout text is not a status");

        CommandSequenceExecutionContext context;
        context.modbusTransport = &transport;
        const auto result = svm::command_sequence::executeCommandSequence(
            modbusReadSequence("seq-transport-then-timeout", 1),
            context);

        assert(result.status == CommandExecutionStatus::Timeout);
        assert(result.steps.size() == 1);
        assert(result.steps[0].status == CommandExecutionStatus::Timeout);
        assert(result.message == "final timeout text is not a status");
        assert(transport.requests.size() == 2);
    }
}

void modbusCancellationDuringExchangeUsesTypedStatus() {
    FakeModbusTransport transport;
    transport.enqueueResult(
        svm::core::modbus::RtuTransportExchangeStatus::TransportError,
        "arbitrary transport text");
    bool cancelRequested = false;
    transport.onExchange = [&cancelRequested] {
        cancelRequested = true;
    };

    CommandSequenceExecutionContext context;
    context.modbusTransport = &transport;
    context.shouldCancel = [&cancelRequested] {
        return cancelRequested;
    };

    const auto result = svm::command_sequence::executeCommandSequence(
        modbusReadSequence("seq-cancel-during-modbus", 0),
        context);

    assert(result.status == CommandExecutionStatus::Cancelled);
    assert(result.steps.size() == 1);
    assert(result.steps[0].status == CommandExecutionStatus::Cancelled);
    assert(hasMetadataValue(result.steps[0].metadata, "modbus_status", "cancelled"));
    assert(transport.requests.size() == 1);
}

void delayBackendAndCancellationContractIsFailClosed() {
    {
        CommandSequence sequence;
        sequence.id = "seq-delay-no-backend";
        sequence.steps = {svm::command_sequence::makeDelayStep("delay", 1)};

        const auto result = svm::command_sequence::executeCommandSequence(sequence, {});
        assert(result.status == CommandExecutionStatus::Failed);
        assert(result.steps.size() == 1);
        assert(result.steps[0].message == "Delay backend is not available.");
        assert(result.evidence.size() == 1);
        assert(result.evidence[0].status == CommandExecutionStatus::Failed);
    }

    {
        CommandSequence sequence;
        sequence.id = "seq-zero-delay";
        sequence.steps = {svm::command_sequence::makeDelayStep("delay", 0)};

        const auto result = svm::command_sequence::executeCommandSequence(sequence, {});
        assert(result.status == CommandExecutionStatus::Completed);
    }

    {
        CommandSequence sequence;
        sequence.id = "seq-cancellable-delay";
        sequence.steps = {svm::command_sequence::makeDelayStep("delay", 250)};
        bool cancelRequested = false;
        std::vector<int> sleepSlices;

        CommandSequenceExecutionContext context;
        context.shouldCancel = [&cancelRequested] {
            return cancelRequested;
        };
        context.sleepForMs = [&](int durationMs) {
            sleepSlices.push_back(durationMs);
            cancelRequested = true;
        };

        const auto result = svm::command_sequence::executeCommandSequence(sequence, context);
        assert(result.status == CommandExecutionStatus::Cancelled);
        assert(result.steps.size() == 1);
        assert(result.steps[0].status == CommandExecutionStatus::Cancelled);
        assert(result.evidence[0].status == CommandExecutionStatus::Cancelled);
        assert((sleepSlices == std::vector<int>{100}));
    }

    {
        CommandSequence sequence;
        sequence.id = "seq-final-slice-cancel";
        sequence.steps = {svm::command_sequence::makeDelayStep("delay", 1)};
        bool cancelRequested = false;

        CommandSequenceExecutionContext context;
        context.shouldCancel = [&cancelRequested] {
            return cancelRequested;
        };
        context.sleepForMs = [&cancelRequested](int durationMs) {
            assert(durationMs == 1);
            cancelRequested = true;
        };

        const auto result = svm::command_sequence::executeCommandSequence(sequence, context);
        assert(result.status == CommandExecutionStatus::Cancelled);
        assert(result.steps[0].status == CommandExecutionStatus::Cancelled);
    }

    {
        CommandSequence sequence;
        sequence.id = "seq-complete-sliced-delay";
        sequence.steps = {svm::command_sequence::makeDelayStep("delay", 250)};
        std::vector<int> sleepSlices;

        CommandSequenceExecutionContext context;
        context.shouldCancel = [] {
            return false;
        };
        context.sleepForMs = [&sleepSlices](int durationMs) {
            sleepSlices.push_back(durationMs);
        };

        const auto result = svm::command_sequence::executeCommandSequence(sequence, context);
        assert(result.status == CommandExecutionStatus::Completed);
        assert((sleepSlices == std::vector<int>{100, 100, 50}));
    }
}

void assertionFailureStopsSequenceAndRecordsResult() {
    CommandSequence sequence;
    sequence.id = "seq-assertion-fail";
    sequence.steps = {
        svm::command_sequence::makeWaitForResponseStep("wait", {0x01}, 100),
        svm::command_sequence::makeAssertionStep("bad-assertion", responseContainsAssertion({0xFF})),
        svm::command_sequence::makeDelayStep("should-not-run", 1),
    };

    CommandSequenceExecutionContext context;
    context.waitForResponse = [](int) -> std::optional<ByteBuffer> {
        return ByteBuffer{0x01, 0x02};
    };

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);

    assert(result.status == CommandExecutionStatus::AssertionFailed);
    assert(result.steps.size() == 2);
    assert(result.steps[1].status == CommandExecutionStatus::AssertionFailed);
    assert(result.evidence[1].type == CommandEvidenceEventType::AssertionResult);
    assert(hasMetadataValue(result.evidence[1].metadata, "passed", "false"));
}

void unsafeCommandIsRejectedBeforeBackendWork() {
    FakeSerialWriteScheduler scheduler(4);
    FakeModbusTransport transport;
    CommandSequence sequence;
    sequence.id = "seq-unsafe";
    sequence.steps = {
        svm::command_sequence::makeModbusReadStep("broadcast-read", ModbusReadCommand{
            .slaveId = 0,
            .functionCode = static_cast<Byte>(svm::core::modbus::ModbusReadFunction::HoldingRegisters),
            .startAddress = 0,
            .quantity = 1,
            .responseTimeoutMs = 100,
            .retryCount = 0,
        }),
    };

    CommandSequenceExecutionContext context;
    context.serialWriteScheduler = &scheduler;
    context.modbusTransport = &transport;

    const auto validation = svm::command_sequence::validateCommandSequence(sequence);
    assert(!validation.ok);
    assert(validation.status == CommandExecutionStatus::RejectedUnsafe);

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);
    assert(result.status == CommandExecutionStatus::RejectedUnsafe);
    assert(result.steps.empty());
    assert(result.evidence.empty());
    assert(scheduler.empty());
    assert(transport.requests.empty());
}

void stableStatusNamesAreTokenLike() {
    assert(std::string(svm::command_sequence::commandExecutionStatusName(CommandExecutionStatus::Completed)) == "completed");
    assert(std::string(svm::command_sequence::commandExecutionStatusName(CommandExecutionStatus::Timeout)) == "timeout");
    assert(std::string(svm::command_sequence::commandExecutionStatusName(CommandExecutionStatus::AssertionFailed)) == "assertion_failed");
    assert(std::string(svm::command_sequence::commandKindName(svm::command_sequence::CommandKind::ModbusRead)) == "modbus_read");
    assert(svm::command_sequence::isTerminalCommandExecutionStatus(CommandExecutionStatus::RejectedUnsafe));
}

} // namespace

int main() {
    validSequenceUsesExistingBackendsAndRecordsEvidence();
    waitForResponseTimeoutStopsSequence();
    cancellationStopsBeforeDispatchingBackendWork();
    modbusRetryClassificationUsesFinalTypedAttempt();
    modbusCancellationDuringExchangeUsesTypedStatus();
    delayBackendAndCancellationContractIsFailClosed();
    assertionFailureStopsSequenceAndRecordsResult();
    unsafeCommandIsRejectedBeforeBackendWork();
    stableStatusNamesAreTokenLike();

    std::cout << "command_sequence_tests passed\n";
    return 0;
}
