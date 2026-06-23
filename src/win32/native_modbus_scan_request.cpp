#include "win32/native_modbus_scan_request.h"

#include <utility>

namespace svm::win32 {

NativeModbusScanRequestResult nativeBuildModbusScanRequest(const NativeModbusScanRequestInput& input) {
    core::modbus::ScanPlanOptions options;
    options.slaveId = input.slaveId;
    options.functionCode = input.functionCode;
    options.range.startAddress = input.startAddress;
    options.range.endAddress = input.endAddress;
    options.blockSize = input.blockSize;
    options.requestIntervalMs = input.requestIntervalMs;
    options.retryCount = input.retryCount;
    options.safetyLevel = input.safetyLevel;

    const auto planResult = core::modbus::buildScanPlan(options);
    if (!planResult.ok) {
        return {.errorMessage = planResult.errorMessage};
    }

    native_storage::ScanExecutionRecord execution;
    execution.session.sessionId = input.scanSessionId;
    execution.session.slaveId = planResult.plan.slaveId;
    execution.session.functionCode = planResult.plan.functionCode;
    execution.session.startAddress = planResult.plan.range.startAddress;
    execution.session.endAddress = planResult.plan.range.endAddress;
    execution.session.blockSize = planResult.plan.blockSize;
    execution.session.requestCount = planResult.plan.requestCount();
    execution.session.status = "running";
    execution.session.startedAtUtc = input.startedAtUtc;

    NativeModbusScanRequestResult result;
    result.ok = true;
    result.request.plan = std::move(planResult.plan);
    result.request.execution = std::move(execution);
    return result;
}

} // namespace svm::win32
