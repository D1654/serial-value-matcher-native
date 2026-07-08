#include "core/modbus_core.h"
#include "core/modbus_scan_executor_core.h"
#include "core/protocol_core.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using svm::core::Byte;
using svm::core::ByteBuffer;

class FakeCoreModbusTransport final : public svm::core::modbus::RtuTransport {
public:
    struct QueuedExchange {
        ByteBuffer expectedRequest;
        svm::core::modbus::RtuTransportExchange exchange;
    };

    void enqueueResponse(ByteBuffer expectedRequest, ByteBuffer responseFrame) {
        svm::core::modbus::RtuTransportExchange exchange;
        exchange.status = svm::core::modbus::RtuTransportExchangeStatus::Success;
        exchange.responseFrame = std::move(responseFrame);
        exchange.sentAtUtc = "2026-07-08T00:00:00Z";
        exchange.receivedAtUtc = "2026-07-08T00:00:01Z";
        exchange.endpoint = "fake://core-modbus";
        exchanges.push_back(QueuedExchange{std::move(expectedRequest), std::move(exchange)});
    }

    void enqueueTimeout(ByteBuffer expectedRequest) {
        svm::core::modbus::RtuTransportExchange exchange;
        exchange.status = svm::core::modbus::RtuTransportExchangeStatus::Timeout;
        exchange.errorMessage = "timeout";
        exchanges.push_back(QueuedExchange{std::move(expectedRequest), std::move(exchange)});
    }

    svm::core::modbus::RtuTransportExchange exchange(svm::core::ByteSpan requestFrame, int responseTimeoutMs) override {
        sentRequests.emplace_back(requestFrame.begin(), requestFrame.end());
        timeoutValues.push_back(responseTimeoutMs);
        if (exchanges.empty()) {
            svm::core::modbus::RtuTransportExchange fallback;
            fallback.status = svm::core::modbus::RtuTransportExchangeStatus::TransportError;
            fallback.requestFrame = ByteBuffer(requestFrame.begin(), requestFrame.end());
            fallback.errorMessage = "no fake response";
            return fallback;
        }

        QueuedExchange queued = std::move(exchanges.front());
        exchanges.erase(exchanges.begin());
        assert((queued.expectedRequest == ByteBuffer(requestFrame.begin(), requestFrame.end())));
        queued.exchange.requestFrame = ByteBuffer(requestFrame.begin(), requestFrame.end());
        return queued.exchange;
    }

    std::vector<QueuedExchange> exchanges;
    std::vector<ByteBuffer> sentRequests;
    std::vector<int> timeoutValues;
};

ByteBuffer normalReadResponse(int slaveId, int functionCode, std::initializer_list<std::uint16_t> values) {
    ByteBuffer body;
    body.push_back(static_cast<Byte>(slaveId));
    body.push_back(static_cast<Byte>(functionCode));
    body.push_back(static_cast<Byte>(values.size() * 2));
    for (const auto value : values) {
        body.push_back(static_cast<Byte>((value >> 8) & 0xFF));
        body.push_back(static_cast<Byte>(value & 0xFF));
    }
    return svm::core::modbus::appendCrc16Modbus(body);
}

void checksumFunctionsMatchKnownValues() {
    const ByteBuffer payload{0x01, 0x02, 0x03, 0xF0};

    assert(svm::core::protocol::sum8(payload) == 0xF6);
    assert(svm::core::protocol::xor8(payload) == 0xF0);
    assert(svm::core::protocol::lrc8(payload) == 0x0A);
}

void crcMatchesKnownModbusVector() {
    const ByteBuffer requestBody{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};

    assert(svm::core::modbus::crc16Modbus(requestBody) == 0xCDC5);

    const auto frame = svm::core::modbus::appendCrc16Modbus(requestBody);
    assert((frame == ByteBuffer{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}));

    const auto check = svm::core::modbus::validateRtuFrame(frame);
    assert(check.ok);
    assert(check.actualCrc == 0xCDC5);
    assert(check.expectedCrc == 0xCDC5);
}

void readRequestFramesSupportedFunctions() {
    const auto request = svm::core::modbus::buildReadRequest(
        1,
        svm::core::modbus::ModbusReadFunction::HoldingRegisters,
        0,
        10);

    assert(request.ok);
    assert((request.frame == ByteBuffer{0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD}));

    const auto invalid = svm::core::modbus::buildReadRequest(0, static_cast<Byte>(0x03), 0, 1);
    assert(!invalid.ok);
    assert(!invalid.errorMessage.empty());
}

void readResponseParsesRegistersAndObservations() {
    const ByteBuffer responseBody{0x01, 0x03, 0x04, 0x12, 0x34, 0xAB, 0xCD};
    const auto responseFrame = svm::core::modbus::appendCrc16Modbus(responseBody);

    const auto parsed = svm::core::modbus::parseReadResponse(responseFrame, 1, 0x03, 100, 2);
    assert(parsed.ok);
    assert(!parsed.isException);
    assert(parsed.slaveId == 1);
    assert(parsed.functionCode == 0x03);
    assert((parsed.registers == std::vector<std::uint16_t>{0x1234, 0xABCD}));
    assert(parsed.observations.size() == 2);
    assert(parsed.observations[0].address == 100);
    assert(parsed.observations[0].value == 0x1234);
    assert(parsed.observations[1].address == 101);
    assert(parsed.observations[1].value == 0xABCD);
}

void readResponseReportsExceptionWithoutSuccess() {
    const ByteBuffer exceptionBody{0x01, 0x83, 0x02};
    const auto exceptionFrame = svm::core::modbus::appendCrc16Modbus(exceptionBody);

    const auto parsed = svm::core::modbus::parseReadResponse(exceptionFrame, 1, 0x03, 0, 1);
    assert(!parsed.ok);
    assert(parsed.isException);
    assert(parsed.exceptionCode == 0x02);
    assert(parsed.exceptionDescription == "illegal data address");
}

void readResponseRejectsMismatchedIdentityAndShape() {
    const ByteBuffer responseBody{0x01, 0x03, 0x04, 0x12, 0x34, 0xAB, 0xCD};
    const auto responseFrame = svm::core::modbus::appendCrc16Modbus(responseBody);

    const auto wrongSlave = svm::core::modbus::parseReadResponse(responseFrame, 2, 0x03, 100, 2);
    assert(!wrongSlave.ok);
    assert(wrongSlave.errorMessage == "Response slave id does not match expected slave id.");

    const auto wrongFunction = svm::core::modbus::parseReadResponse(responseFrame, 1, 0x04, 100, 2);
    assert(!wrongFunction.ok);
    assert(wrongFunction.errorMessage == "Response function code does not match expected function code.");

    const auto wrongQuantity = svm::core::modbus::parseReadResponse(responseFrame, 1, 0x03, 100, 1);
    assert(!wrongQuantity.ok);
    assert(wrongQuantity.errorMessage == "Response register count does not match expected quantity.");
}

void scanPlanSplitsRangeAndBuildsRequestFrames() {
    svm::core::modbus::ScanPlanOptions options;
    options.slaveId = 7;
    options.functionCode = 0x04;
    options.range = {10, 14};
    options.blockSize = 2;
    options.requestIntervalMs = 15;
    options.retryCount = 1;

    const auto result = svm::core::modbus::buildScanPlan(options);
    assert(result.ok);
    assert(result.plan.registerCount() == 5);
    assert(result.plan.requestCount() == 3);
    assert(result.plan.estimatedAttemptCount() == 6);
    assert(result.plan.estimatedInterRequestDelayMs() == 30);

    assert(result.plan.blocks[0].startAddress == 10);
    assert(result.plan.blocks[0].endAddress == 11);
    assert(result.plan.blocks[0].quantity == 2);
    assert(result.plan.blocks[1].startAddress == 12);
    assert(result.plan.blocks[1].endAddress == 13);
    assert(result.plan.blocks[2].startAddress == 14);
    assert(result.plan.blocks[2].endAddress == 14);

    for (const auto& block : result.plan.blocks) {
        assert(svm::core::modbus::validateRtuFrame(block.requestFrame).ok);
    }
}

void scanPlanRejectsUnsafeRangesAndRetrySettings() {
    svm::core::modbus::ScanPlanOptions options;
    options.slaveId = 1;
    options.functionCode = 0x03;
    options.range = {10, 9};
    options.blockSize = 16;
    assert(!svm::core::modbus::buildScanPlan(options).ok);

    options.range = {0, 4096};
    assert(!svm::core::modbus::buildScanPlan(options).ok);

    options.range = {0, 15};
    options.blockSize = 65;
    assert(!svm::core::modbus::buildScanPlan(options).ok);

    options.blockSize = 16;
    options.retryCount = 6;
    assert(!svm::core::modbus::buildScanPlan(options).ok);
}

void coreScanExecutorRetriesTimeoutThenUsesSuccessfulAttempt() {
    svm::core::modbus::ScanPlanOptions planOptions;
    planOptions.slaveId = 1;
    planOptions.functionCode = 0x03;
    planOptions.range = {0, 0};
    planOptions.blockSize = 1;
    planOptions.retryCount = 1;
    const auto planResult = svm::core::modbus::buildScanPlan(planOptions);
    assert(planResult.ok);

    FakeCoreModbusTransport transport;
    transport.enqueueTimeout(planResult.plan.blocks[0].requestFrame);
    transport.enqueueResponse(planResult.plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {42}));

    int progressCount = 0;
    svm::core::modbus::ScanExecutionProgress lastProgress;
    svm::core::modbus::ScanExecutionOptions executionOptions;
    executionOptions.responseTimeoutMs = 250;
    executionOptions.onProgress = [&](const svm::core::modbus::ScanExecutionProgress& progress) {
        ++progressCount;
        lastProgress = progress;
    };

    svm::core::modbus::ScanExecutor executor(transport);
    const auto result = executor.execute(planResult.plan, executionOptions);

    assert(result.status == svm::core::modbus::ScanExecutionStatus::Completed);
    assert(result.successBlockCount == 1);
    assert(result.failedBlockCount == 0);
    assert(result.blocks.size() == 1);
    assert(result.blocks[0].attempts.size() == 2);
    assert(result.blocks[0].attempts[0].status == svm::core::modbus::ScanAttemptStatus::Timeout);
    assert(result.blocks[0].attempts[1].status == svm::core::modbus::ScanAttemptStatus::Success);
    assert(result.observations.size() == 1);
    assert(result.observations[0].value == 42);
    assert((transport.timeoutValues == std::vector<int>{250, 250}));
    assert(progressCount == 1);
    assert(lastProgress.completedBlocks == 1);
    assert(lastProgress.observations == 1);
}

void coreScanExecutorCancellationStopsBeforeSendingRequests() {
    svm::core::modbus::ScanPlanOptions planOptions;
    planOptions.slaveId = 1;
    planOptions.functionCode = 0x03;
    planOptions.range = {0, 1};
    planOptions.blockSize = 2;
    const auto planResult = svm::core::modbus::buildScanPlan(planOptions);
    assert(planResult.ok);

    FakeCoreModbusTransport transport;
    bool progressPublished = false;
    svm::core::modbus::ScanExecutionOptions executionOptions;
    executionOptions.shouldCancel = []() {
        return true;
    };
    executionOptions.onProgress = [&](const svm::core::modbus::ScanExecutionProgress& progress) {
        progressPublished = true;
        assert(progress.completedBlocks == 0);
        assert(progress.totalBlocks == 1);
    };

    svm::core::modbus::ScanExecutor executor(transport);
    const auto result = executor.execute(planResult.plan, executionOptions);

    assert(result.status == svm::core::modbus::ScanExecutionStatus::Failed);
    assert(result.errorMessage == "Scan was cancelled.");
    assert(result.blocks.empty());
    assert(transport.sentRequests.empty());
    assert(progressPublished);
}

} // namespace

int main() {
    checksumFunctionsMatchKnownValues();
    crcMatchesKnownModbusVector();
    readRequestFramesSupportedFunctions();
    readResponseParsesRegistersAndObservations();
    readResponseReportsExceptionWithoutSuccess();
    readResponseRejectsMismatchedIdentityAndShape();
    scanPlanSplitsRangeAndBuildsRequestFrames();
    scanPlanRejectsUnsafeRangesAndRetrySettings();
    coreScanExecutorRetriesTimeoutThenUsesSuccessfulAttempt();
    coreScanExecutorCancellationStopsBeforeSendingRequests();

    std::cout << "native_protocol_modbus_tests passed\n";
    return 0;
}
