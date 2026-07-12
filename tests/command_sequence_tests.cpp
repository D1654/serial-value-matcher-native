#include "command_sequence/command_sequence.h"

#include <cassert>
#include <cstdint>
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
            return result;
        }

        const int slaveId = requestFrame[0];
        const Byte functionCode = requestFrame[1];
        const int startAddress = (static_cast<int>(requestFrame[2]) << 8) | static_cast<int>(requestFrame[3]);
        const int quantity = (static_cast<int>(requestFrame[4]) << 8) | static_cast<int>(requestFrame[5]);
        result.responseFrame = modbusReadResponse(slaveId, functionCode, startAddress, quantity);
        return result;
    }

    std::vector<ByteBuffer> requests;
    std::vector<int> timeoutValues;
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

bool hasMetadataValue(
    const std::map<svm::core::Text, svm::core::Text>& metadata,
    const std::string& key,
    const std::string& expected) {
    const auto found = metadata.find(key);
    return found != metadata.end() && found->second == expected;
}

void validSequenceUsesExistingBackendsAndRecordsEvidence() {
    svm::transport::SerialWriteQueue queue(4);
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
    context.serialWriteTransport = &queue;
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
    assert(hasMetadataValue(result.steps.front().metadata, "backend", "serial_write_transport"));
    assert(queue.pendingCount() == 1);
    assert(queue.peek()->payload == ByteBuffer({0x10, 0x02}));
    assert(queue.peek()->timeoutMs == 200);
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
    svm::transport::SerialWriteQueue queue(4);
    CommandSequence sequence;
    sequence.id = "seq-cancel";
    sequence.steps = {
        svm::command_sequence::makeSerialWriteStep("send", {0x01}, 100),
    };

    CommandSequenceExecutionContext context;
    context.serialWriteTransport = &queue;
    context.shouldCancel = [] {
        return true;
    };

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);

    assert(result.status == CommandExecutionStatus::Cancelled);
    assert(result.steps.size() == 1);
    assert(result.steps[0].status == CommandExecutionStatus::Cancelled);
    assert(queue.empty());
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
    svm::transport::SerialWriteQueue queue(4);
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
    context.serialWriteTransport = &queue;
    context.modbusTransport = &transport;

    const auto validation = svm::command_sequence::validateCommandSequence(sequence);
    assert(!validation.ok);
    assert(validation.status == CommandExecutionStatus::RejectedUnsafe);

    const auto result = svm::command_sequence::executeCommandSequence(sequence, context);
    assert(result.status == CommandExecutionStatus::RejectedUnsafe);
    assert(result.steps.empty());
    assert(result.evidence.empty());
    assert(queue.empty());
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
    assertionFailureStopsSequenceAndRecordsResult();
    unsafeCommandIsRejectedBeforeBackendWork();
    stableStatusNamesAreTokenLike();

    std::cout << "command_sequence_tests passed\n";
    return 0;
}
