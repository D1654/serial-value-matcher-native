#pragma once

#include "core/modbus_core.h"
#include "native_storage/native_session_store.h"

#include <string>

namespace svm::win32 {

struct NativeModbusScanRequestInput {
    int slaveId = 1;
    core::Byte functionCode = static_cast<core::Byte>(core::modbus::ModbusReadFunction::HoldingRegisters);
    int startAddress = 0;
    int endAddress = 15;
    int blockSize = 16;
    int requestIntervalMs = 30;
    int retryCount = 0;
    core::modbus::ScanSafetyLevel safetyLevel = core::modbus::ScanSafetyLevel::Custom;
    std::string scanSessionId;
    std::string startedAtUtc;
};

struct NativeModbusScanRequest {
    core::modbus::ScanPlan plan;
    native_storage::ScanExecutionRecord execution;
};

struct NativeModbusScanRequestResult {
    bool ok = false;
    std::string errorMessage;
    NativeModbusScanRequest request;
};

NativeModbusScanRequestResult nativeBuildModbusScanRequest(const NativeModbusScanRequestInput& input);

} // namespace svm::win32
