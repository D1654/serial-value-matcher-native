#pragma once

#include "core/byte_buffer.h"
#include "core/text.h"

#include <cstdint>
#include <vector>

namespace svm::core::modbus {

enum class ModbusReadFunction : Byte {
    HoldingRegisters = 0x03,
    InputRegisters = 0x04
};

enum class ScanSafetyLevel {
    Conservative,
    Balanced,
    Aggressive,
    Custom
};

struct RtuFrameCheck {
    bool ok = false;
    Text errorMessage;
    ByteBuffer bodyWithoutCrc;
    std::uint16_t actualCrc = 0;
    std::uint16_t expectedCrc = 0;
};

struct BuildReadRequestResult {
    bool ok = false;
    Text errorMessage;
    ByteBuffer frame;
};

struct RegisterObservation {
    std::uint16_t address = 0;
    std::uint16_t value = 0;
};

struct ParseReadResponseResult {
    bool ok = false;
    Text errorMessage;
    Byte slaveId = 0;
    Byte functionCode = 0;
    bool isException = false;
    Byte exceptionCode = 0;
    Text exceptionDescription;
    ByteBuffer bodyWithoutCrc;
    std::vector<std::uint16_t> registers;
    std::vector<RegisterObservation> observations;
};

struct ScanRange {
    int startAddress = 0;
    int endAddress = 0;
};

struct ScanPlanOptions {
    int slaveId = 1;
    Byte functionCode = static_cast<Byte>(ModbusReadFunction::HoldingRegisters);
    ScanRange range;
    int blockSize = 32;
    int requestIntervalMs = 30;
    int retryCount = 0;
    ScanSafetyLevel safetyLevel = ScanSafetyLevel::Conservative;
};

struct ScanBlock {
    int index = 0;
    int startAddress = 0;
    int endAddress = 0;
    int quantity = 0;
    ByteBuffer requestFrame;
};

struct ScanPlan {
    int slaveId = 0;
    Byte functionCode = 0;
    ScanRange range;
    int blockSize = 0;
    int requestIntervalMs = 0;
    int retryCount = 0;
    ScanSafetyLevel safetyLevel = ScanSafetyLevel::Conservative;
    std::vector<ScanBlock> blocks;

    [[nodiscard]] int registerCount() const;
    [[nodiscard]] int requestCount() const;
    [[nodiscard]] int estimatedAttemptCount() const;
    [[nodiscard]] int estimatedInterRequestDelayMs() const;
};

struct BuildScanPlanResult {
    bool ok = false;
    Text errorMessage;
    ScanPlan plan;
};

std::uint16_t crc16Modbus(ByteSpan data);
ByteBuffer appendCrc16Modbus(ByteSpan bodyWithoutCrc);
RtuFrameCheck validateRtuFrame(ByteSpan frame);
ByteBuffer bodyWithoutCrc(ByteSpan frame);
std::uint16_t readFrameCrcLittleEndian(ByteSpan frame);
Text formatCrc16(std::uint16_t value);

bool isSupportedReadFunction(Byte functionCode);
Text describeReadFunction(Byte functionCode);
BuildReadRequestResult buildReadRequest(int slaveId, ModbusReadFunction function, int startAddress, int quantity);
BuildReadRequestResult buildReadRequest(int slaveId, Byte functionCode, int startAddress, int quantity);

ParseReadResponseResult parseReadResponse(
    ByteSpan frame,
    int expectedSlaveId,
    Byte expectedFunctionCode,
    int expectedStartAddress,
    int expectedQuantity);

Text describeModbusException(Byte exceptionCode);
Text describeScanSafetyLevel(ScanSafetyLevel level);
BuildScanPlanResult buildScanPlan(const ScanPlanOptions& options);

} // namespace svm::core::modbus
