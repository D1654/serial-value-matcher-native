#include "modbus/modbus_scan_plan.h"

#include <algorithm>
#include <utility>

namespace svm::modbus {
namespace {

constexpr int MinimumAddress = 0;
constexpr int MaximumAddress = 0xFFFF;
constexpr int MinimumBlockSize = 1;
constexpr int MaximumSafeBlockSize = 64;
constexpr int MaximumPlanRegisters = 4096;
constexpr int MinimumRetryCount = 0;
constexpr int MaximumRetryCount = 5;

BuildScanPlanResult fail(ScanPlanBuildStatus status, QString message) {
    BuildScanPlanResult result;
    result.status = status;
    result.errorMessage = std::move(message);
    return result;
}

} // namespace

int ScanPlan::registerCount() const {
    if (range.endAddress < range.startAddress) {
        return 0;
    }
    return range.endAddress - range.startAddress + 1;
}

int ScanPlan::requestCount() const {
    return blocks.size();
}

int ScanPlan::estimatedAttemptCount() const {
    return requestCount() * (retryCount + 1);
}

int ScanPlan::estimatedInterRequestDelayMs() const {
    return std::max(0, requestCount() - 1) * requestIntervalMs;
}

QString describeScanSafetyLevel(ScanSafetyLevel level) {
    switch (level) {
    case ScanSafetyLevel::Conservative:
        return QStringLiteral("保守：小块、低速，适合未知设备首扫");
    case ScanSafetyLevel::Balanced:
        return QStringLiteral("均衡：中等块大小和间隔，适合已知稳定设备");
    case ScanSafetyLevel::Aggressive:
        return QStringLiteral("激进：更高请求密度，仅适合确认可承受的设备");
    case ScanSafetyLevel::Custom:
        return QStringLiteral("自定义：使用用户指定的块大小、间隔和重试次数");
    }

    return QStringLiteral("未知安全等级");
}

QString describeScanPlanBuildStatus(ScanPlanBuildStatus status) {
    switch (status) {
    case ScanPlanBuildStatus::Success:
        return QStringLiteral("扫描计划有效");
    case ScanPlanBuildStatus::InvalidSlaveId:
        return QStringLiteral("从站 ID 无效");
    case ScanPlanBuildStatus::UnsupportedFunction:
        return QStringLiteral("功能码不支持");
    case ScanPlanBuildStatus::InvalidAddressRange:
        return QStringLiteral("地址范围无效");
    case ScanPlanBuildStatus::InvalidBlockSize:
        return QStringLiteral("块大小无效");
    case ScanPlanBuildStatus::InvalidRequestInterval:
        return QStringLiteral("请求间隔无效");
    case ScanPlanBuildStatus::InvalidRetryCount:
        return QStringLiteral("重试次数无效");
    case ScanPlanBuildStatus::PlanTooLarge:
        return QStringLiteral("扫描计划过大");
    case ScanPlanBuildStatus::RequestBuildFailed:
        return QStringLiteral("请求帧构造失败");
    }

    return QStringLiteral("未知扫描计划状态");
}

BuildScanPlanResult buildScanPlan(const ScanPlanOptions& options) {
    if (options.slaveId < 1 || options.slaveId > 247) {
        return fail(
            ScanPlanBuildStatus::InvalidSlaveId,
            QStringLiteral("扫描计划无效：从站 ID 必须是 1-247，不能使用广播地址 0。"));
    }

    if (!isSupportedReadFunction(options.functionCode)) {
        return fail(ScanPlanBuildStatus::UnsupportedFunction, QStringLiteral("扫描计划无效：只允许 FC03/FC04 只读功能码，当前为 %1。")
            .arg(describeReadFunction(options.functionCode)));
    }

    if (options.range.startAddress < MinimumAddress || options.range.startAddress > MaximumAddress) {
        return fail(ScanPlanBuildStatus::InvalidAddressRange, QStringLiteral("扫描计划无效：起始地址必须在 0-65535 范围内。"));
    }

    if (options.range.endAddress < MinimumAddress || options.range.endAddress > MaximumAddress) {
        return fail(ScanPlanBuildStatus::InvalidAddressRange, QStringLiteral("扫描计划无效：结束地址必须在 0-65535 范围内。"));
    }

    if (options.range.endAddress < options.range.startAddress) {
        return fail(ScanPlanBuildStatus::InvalidAddressRange, QStringLiteral("扫描计划无效：结束地址不能小于起始地址。"));
    }

    if (options.blockSize < MinimumBlockSize || options.blockSize > MaximumSafeBlockSize) {
        return fail(
            ScanPlanBuildStatus::InvalidBlockSize,
            QStringLiteral("扫描计划无效：块大小必须在 1-64 个寄存器之间，避免单次请求过大。"));
    }

    if (options.requestIntervalMs < 0) {
        return fail(ScanPlanBuildStatus::InvalidRequestInterval, QStringLiteral("扫描计划无效：请求间隔不能为负数。"));
    }

    if (options.retryCount < MinimumRetryCount || options.retryCount > MaximumRetryCount) {
        return fail(ScanPlanBuildStatus::InvalidRetryCount, QStringLiteral("扫描计划无效：重试次数必须在 0-5 之间。"));
    }

    const int totalRegisters = options.range.endAddress - options.range.startAddress + 1;
    if (totalRegisters > MaximumPlanRegisters) {
        return fail(
            ScanPlanBuildStatus::PlanTooLarge,
            QStringLiteral("扫描计划无效：一次计划最多允许 %1 个寄存器；请缩小扫描范围后分批扫描。").arg(MaximumPlanRegisters));
    }

    ScanPlan plan;
    plan.slaveId = options.slaveId;
    plan.functionCode = options.functionCode;
    plan.range = options.range;
    plan.blockSize = options.blockSize;
    plan.requestIntervalMs = options.requestIntervalMs;
    plan.retryCount = options.retryCount;
    plan.safetyLevel = options.safetyLevel;
    plan.blocks.reserve((totalRegisters + options.blockSize - 1) / options.blockSize);

    int currentAddress = options.range.startAddress;
    int blockIndex = 0;
    while (currentAddress <= options.range.endAddress) {
        const int quantity = std::min(options.blockSize, options.range.endAddress - currentAddress + 1);
        const auto request = buildReadRequest(options.slaveId, options.functionCode, currentAddress, quantity);
        if (!request.ok) {
            return fail(ScanPlanBuildStatus::RequestBuildFailed, QStringLiteral("扫描计划无效：第 %1 个请求块构造失败：%2")
                .arg(blockIndex + 1)
                .arg(request.errorMessage));
        }

        plan.blocks.append(ScanBlock{
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
    result.status = ScanPlanBuildStatus::Success;
    result.plan = plan;
    return result;
}

} // namespace svm::modbus
