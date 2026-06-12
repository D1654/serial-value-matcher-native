#include "core/modbus_core.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace svm::core::modbus {
namespace {

constexpr int MinimumRtuFrameSize = 4;
constexpr int MinimumSlaveId = 1;
constexpr int MaximumSlaveId = 247;
constexpr int MinimumRegisterAddress = 0;
constexpr int MaximumRegisterAddress = 0xFFFF;
constexpr int MinimumQuantity = 1;
constexpr int MaximumReadQuantity = 125;
constexpr int MinimumBlockSize = 1;
constexpr int MaximumSafeBlockSize = 64;
constexpr int MaximumPlanRegisters = 4096;
constexpr int MinimumRetryCount = 0;
constexpr int MaximumRetryCount = 5;

BuildReadRequestResult failRead(Text message) {
    BuildReadRequestResult result;
    result.errorMessage = std::move(message);
    return result;
}

ParseReadResponseResult failResponse(Text message) {
    ParseReadResponseResult result;
    result.errorMessage = std::move(message);
    return result;
}

BuildScanPlanResult failPlan(Text message) {
    BuildScanPlanResult result;
    result.errorMessage = std::move(message);
    return result;
}

void appendUInt16BigEndian(ByteBuffer& data, int value) {
    data.push_back(static_cast<Byte>((value >> 8) & 0xFF));
    data.push_back(static_cast<Byte>(value & 0xFF));
}

std::uint16_t readUInt16BigEndian(ByteSpan data, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1]);
}

} // namespace

std::uint16_t crc16Modbus(ByteSpan data) {
    std::uint16_t crc = 0xFFFF;
    for (const Byte byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const bool lsbSet = (crc & 0x0001u) != 0;
            crc >>= 1;
            if (lsbSet) {
                crc ^= 0xA001u;
            }
        }
    }
    return crc;
}

ByteBuffer appendCrc16Modbus(ByteSpan bodyWithoutCrcValue) {
    ByteBuffer frame(bodyWithoutCrcValue.begin(), bodyWithoutCrcValue.end());
    const std::uint16_t crc = crc16Modbus(bodyWithoutCrcValue);
    frame.push_back(static_cast<Byte>(crc & 0x00FFu));
    frame.push_back(static_cast<Byte>((crc >> 8) & 0x00FFu));
    return frame;
}

RtuFrameCheck validateRtuFrame(ByteSpan frame) {
    RtuFrameCheck result;
    if (frame.size() < MinimumRtuFrameSize) {
        result.errorMessage = "RTU frame is too short.";
        return result;
    }

    result.bodyWithoutCrc = bodyWithoutCrc(frame);
    result.actualCrc = readFrameCrcLittleEndian(frame);
    result.expectedCrc = crc16Modbus(result.bodyWithoutCrc);
    if (result.actualCrc != result.expectedCrc) {
        result.errorMessage = "RTU CRC check failed.";
        return result;
    }

    result.ok = true;
    return result;
}

ByteBuffer bodyWithoutCrc(ByteSpan frame) {
    if (frame.size() <= 2) {
        return {};
    }
    return {frame.begin(), frame.end() - 2};
}

std::uint16_t readFrameCrcLittleEndian(ByteSpan frame) {
    if (frame.size() < 2) {
        return 0;
    }

    const auto lowIndex = frame.size() - 2;
    const auto highIndex = frame.size() - 1;
    return static_cast<std::uint16_t>(frame[lowIndex] | (static_cast<std::uint16_t>(frame[highIndex]) << 8));
}

Text formatCrc16(std::uint16_t value) {
    char buffer[7] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%04X", static_cast<unsigned int>(value));
    return buffer;
}

bool isSupportedReadFunction(Byte functionCode) {
    return functionCode == static_cast<Byte>(ModbusReadFunction::HoldingRegisters)
        || functionCode == static_cast<Byte>(ModbusReadFunction::InputRegisters);
}

Text describeReadFunction(Byte functionCode) {
    switch (functionCode) {
    case static_cast<Byte>(ModbusReadFunction::HoldingRegisters):
        return "FC03 holding registers";
    case static_cast<Byte>(ModbusReadFunction::InputRegisters):
        return "FC04 input registers";
    default:
        return "unsupported read function";
    }
}

BuildReadRequestResult buildReadRequest(int slaveId, ModbusReadFunction function, int startAddress, int quantity) {
    return buildReadRequest(slaveId, static_cast<Byte>(function), startAddress, quantity);
}

BuildReadRequestResult buildReadRequest(int slaveId, Byte functionCode, int startAddress, int quantity) {
    if (slaveId < MinimumSlaveId || slaveId > MaximumSlaveId) {
        return failRead("Invalid slave id.");
    }

    if (!isSupportedReadFunction(functionCode)) {
        return failRead("Invalid read function.");
    }

    if (startAddress < MinimumRegisterAddress || startAddress > MaximumRegisterAddress) {
        return failRead("Invalid start address.");
    }

    if (quantity < MinimumQuantity || quantity > MaximumReadQuantity) {
        return failRead("Invalid read quantity.");
    }

    if (startAddress + quantity - 1 > MaximumRegisterAddress) {
        return failRead("Invalid read address range.");
    }

    ByteBuffer body;
    body.reserve(6);
    body.push_back(static_cast<Byte>(slaveId));
    body.push_back(functionCode);
    appendUInt16BigEndian(body, startAddress);
    appendUInt16BigEndian(body, quantity);

    BuildReadRequestResult result;
    result.ok = true;
    result.frame = appendCrc16Modbus(body);
    return result;
}

ParseReadResponseResult parseReadResponse(
    ByteSpan frame,
    int expectedSlaveId,
    Byte expectedFunctionCode,
    int expectedStartAddress,
    int expectedQuantity) {
    const auto frameCheck = validateRtuFrame(frame);
    if (!frameCheck.ok) {
        return failResponse(frameCheck.errorMessage);
    }

    const ByteBuffer& body = frameCheck.bodyWithoutCrc;
    if (body.size() < 3) {
        return failResponse("Modbus response is too short.");
    }

    ParseReadResponseResult result;
    result.bodyWithoutCrc = body;
    result.slaveId = body[0];
    result.functionCode = body[1];

    if (expectedSlaveId < MinimumSlaveId || expectedSlaveId > MaximumSlaveId) {
        return failResponse("Invalid expected slave id.");
    }

    if (!isSupportedReadFunction(expectedFunctionCode)) {
        return failResponse("Invalid expected read function.");
    }

    if (expectedStartAddress < MinimumRegisterAddress || expectedStartAddress > MaximumRegisterAddress
        || expectedQuantity < MinimumQuantity) {
        return failResponse("Invalid expected address or quantity.");
    }

    if (result.slaveId != static_cast<Byte>(expectedSlaveId)) {
        return failResponse("Response slave id does not match expected slave id.");
    }

    const Byte expectedExceptionFunction = static_cast<Byte>(expectedFunctionCode | 0x80u);
    if (result.functionCode == expectedExceptionFunction) {
        if (body.size() != 3) {
            return failResponse("Invalid Modbus exception response length.");
        }
        result.isException = true;
        result.exceptionCode = body[2];
        result.exceptionDescription = describeModbusException(result.exceptionCode);
        result.errorMessage = "Device returned a Modbus exception: " + result.exceptionDescription + ".";
        return result;
    }

    if (result.functionCode != expectedFunctionCode) {
        return failResponse("Response function code does not match expected function code.");
    }

    const int byteCount = body[2];
    const int payloadSize = static_cast<int>(body.size()) - 3;
    if (byteCount != payloadSize) {
        return failResponse("Response byte count does not match payload size.");
    }

    if (byteCount % 2 != 0) {
        return failResponse("Response byte count must be even.");
    }

    const int registerCount = byteCount / 2;
    if (registerCount != expectedQuantity) {
        return failResponse("Response register count does not match expected quantity.");
    }

    if (expectedStartAddress + registerCount - 1 > MaximumRegisterAddress) {
        return failResponse("Response address range is invalid.");
    }

    result.registers.reserve(registerCount);
    result.observations.reserve(registerCount);
    for (int index = 0; index < registerCount; ++index) {
        const auto value = readUInt16BigEndian(body, static_cast<std::size_t>(3 + index * 2));
        result.registers.push_back(value);
        result.observations.push_back(RegisterObservation{
            static_cast<std::uint16_t>(expectedStartAddress + index),
            value
        });
    }

    result.ok = true;
    return result;
}

Text describeModbusException(Byte exceptionCode) {
    switch (exceptionCode) {
    case 0x01:
        return "illegal function";
    case 0x02:
        return "illegal data address";
    case 0x03:
        return "illegal data value";
    case 0x04:
        return "slave device failure";
    case 0x05:
        return "acknowledge";
    case 0x06:
        return "slave device busy";
    default:
        return "unknown exception";
    }
}

int ScanPlan::registerCount() const {
    if (range.endAddress < range.startAddress) {
        return 0;
    }
    return range.endAddress - range.startAddress + 1;
}

int ScanPlan::requestCount() const {
    return static_cast<int>(blocks.size());
}

int ScanPlan::estimatedAttemptCount() const {
    return requestCount() * (retryCount + 1);
}

int ScanPlan::estimatedInterRequestDelayMs() const {
    return std::max(0, requestCount() - 1) * requestIntervalMs;
}

Text describeScanSafetyLevel(ScanSafetyLevel level) {
    switch (level) {
    case ScanSafetyLevel::Conservative:
        return "conservative";
    case ScanSafetyLevel::Balanced:
        return "balanced";
    case ScanSafetyLevel::Aggressive:
        return "aggressive";
    case ScanSafetyLevel::Custom:
        return "custom";
    }

    return "unknown";
}

BuildScanPlanResult buildScanPlan(const ScanPlanOptions& options) {
    if (options.slaveId < MinimumSlaveId || options.slaveId > MaximumSlaveId) {
        return failPlan("Invalid scan slave id.");
    }

    if (!isSupportedReadFunction(options.functionCode)) {
        return failPlan("Invalid scan read function.");
    }

    if (options.range.startAddress < MinimumRegisterAddress || options.range.startAddress > MaximumRegisterAddress) {
        return failPlan("Invalid scan start address.");
    }

    if (options.range.endAddress < MinimumRegisterAddress || options.range.endAddress > MaximumRegisterAddress) {
        return failPlan("Invalid scan end address.");
    }

    if (options.range.endAddress < options.range.startAddress) {
        return failPlan("Scan end address must not be smaller than start address.");
    }

    if (options.blockSize < MinimumBlockSize || options.blockSize > MaximumSafeBlockSize) {
        return failPlan("Invalid scan block size.");
    }

    if (options.requestIntervalMs < 0) {
        return failPlan("Scan request interval must not be negative.");
    }

    if (options.retryCount < MinimumRetryCount || options.retryCount > MaximumRetryCount) {
        return failPlan("Invalid scan retry count.");
    }

    const int totalRegisters = options.range.endAddress - options.range.startAddress + 1;
    if (totalRegisters > MaximumPlanRegisters) {
        return failPlan("Scan plan is too large.");
    }

    ScanPlan plan;
    plan.slaveId = options.slaveId;
    plan.functionCode = options.functionCode;
    plan.range = options.range;
    plan.blockSize = options.blockSize;
    plan.requestIntervalMs = options.requestIntervalMs;
    plan.retryCount = options.retryCount;
    plan.safetyLevel = options.safetyLevel;
    plan.blocks.reserve(static_cast<std::size_t>((totalRegisters + options.blockSize - 1) / options.blockSize));

    int currentAddress = options.range.startAddress;
    int blockIndex = 0;
    while (currentAddress <= options.range.endAddress) {
        const int quantity = std::min(options.blockSize, options.range.endAddress - currentAddress + 1);
        const auto request = buildReadRequest(options.slaveId, options.functionCode, currentAddress, quantity);
        if (!request.ok) {
            return failPlan("Failed to build scan request block.");
        }

        plan.blocks.push_back(ScanBlock{
            blockIndex,
            currentAddress,
            currentAddress + quantity - 1,
            quantity,
            request.frame
        });

        currentAddress += quantity;
        ++blockIndex;
    }

    BuildScanPlanResult result;
    result.ok = true;
    result.plan = std::move(plan);
    return result;
}

} // namespace svm::core::modbus
