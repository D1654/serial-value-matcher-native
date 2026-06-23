#include "win32/native_modbus_scan_request.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

void validInputBuildsPlanAndRunningExecution() {
    svm::win32::NativeModbusScanRequestInput input;
    input.slaveId = 7;
    input.functionCode = static_cast<svm::core::Byte>(svm::core::modbus::ModbusReadFunction::InputRegisters);
    input.startAddress = 40000;
    input.endAddress = 40031;
    input.blockSize = 16;
    input.requestIntervalMs = 30;
    input.retryCount = 0;
    input.scanSessionId = "scan-test-001";
    input.startedAtUtc = "2026-06-23T10:30:00Z";

    const auto result = svm::win32::nativeBuildModbusScanRequest(input);
    assert(result.ok);
    assert(result.errorMessage.empty());
    assert(result.request.plan.slaveId == 7);
    assert(result.request.plan.functionCode == 0x04);
    assert(result.request.plan.blocks.size() == 2);
    assert(result.request.plan.requestCount() == 2);

    const auto& session = result.request.execution.session;
    assert(session.sessionId == "scan-test-001");
    assert(session.slaveId == 7);
    assert(session.functionCode == 0x04);
    assert(session.startAddress == 40000);
    assert(session.endAddress == 40031);
    assert(session.blockSize == 16);
    assert(session.requestCount == 2);
    assert(session.status == "running");
    assert(session.startedAtUtc == "2026-06-23T10:30:00Z");
}

void invalidInputReturnsPlanError() {
    svm::win32::NativeModbusScanRequestInput input;
    input.slaveId = 0;
    input.scanSessionId = "scan-invalid";
    input.startedAtUtc = "2026-06-23T10:30:00Z";

    const auto result = svm::win32::nativeBuildModbusScanRequest(input);
    assert(!result.ok);
    assert(result.errorMessage == "Invalid scan slave id.");
    assert(result.request.execution.session.sessionId.empty());
}

void defaultInputPreservesCurrentNativeScanDefaults() {
    svm::win32::NativeModbusScanRequestInput input;
    input.scanSessionId = "scan-default";
    input.startedAtUtc = "2026-06-23T10:30:00Z";

    const auto result = svm::win32::nativeBuildModbusScanRequest(input);
    assert(result.ok);
    assert(result.request.plan.slaveId == 1);
    assert(result.request.plan.functionCode == 0x03);
    assert(result.request.plan.range.startAddress == 0);
    assert(result.request.plan.range.endAddress == 15);
    assert(result.request.plan.blockSize == 16);
    assert(result.request.plan.requestIntervalMs == 30);
    assert(result.request.plan.retryCount == 0);
    assert(result.request.plan.safetyLevel == svm::core::modbus::ScanSafetyLevel::Custom);
    assert(result.request.execution.session.requestCount == 1);
}

} // namespace

int main() {
    validInputBuildsPlanAndRunningExecution();
    invalidInputReturnsPlanError();
    defaultInputPreservesCurrentNativeScanDefaults();

    std::cout << "native_modbus_scan_request_tests passed\n";
    return 0;
}
