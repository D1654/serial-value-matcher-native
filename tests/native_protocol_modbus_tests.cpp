#include "core/modbus_core.h"
#include "core/protocol_core.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using svm::core::Byte;
using svm::core::ByteBuffer;

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

} // namespace

int main() {
    checksumFunctionsMatchKnownValues();
    crcMatchesKnownModbusVector();
    readRequestFramesSupportedFunctions();
    readResponseParsesRegistersAndObservations();
    readResponseReportsExceptionWithoutSuccess();
    scanPlanSplitsRangeAndBuildsRequestFrames();

    std::cout << "native_protocol_modbus_tests passed\n";
    return 0;
}
