#include "modbus/modbus_scan_executor.h"

#include "modbus/modbus_read_request.h"

#include <algorithm>
#include <utility>

namespace svm::modbus {
namespace {

QString invalidPlanMessage(const ScanPlan& plan, const ScanExecutionOptions& options) {
    if (plan.slaveId < 1 || plan.slaveId > 247) {
        return QStringLiteral("扫描执行失败：从站 ID 必须是 1-247，不能使用广播地址 0。");
    }

    if (!isSupportedReadFunction(plan.functionCode)) {
        return QStringLiteral("扫描执行失败：只允许 FC03/FC04 只读功能码，当前为 %1。")
            .arg(describeReadFunction(plan.functionCode));
    }

    if (plan.blocks.isEmpty()) {
        return QStringLiteral("扫描执行失败：扫描计划没有任何请求块。");
    }

    if (plan.retryCount < 0) {
        return QStringLiteral("扫描执行失败：重试次数不能为负数。");
    }

    if (options.responseTimeoutMs < 0) {
        return QStringLiteral("扫描执行失败：响应超时时间不能为负数。");
    }

    for (const auto& block : plan.blocks) {
        if (block.requestFrame.isEmpty()) {
            return QStringLiteral("扫描执行失败：第 %1 个请求块缺少原始请求帧。").arg(block.index + 1);
        }

        if (block.startAddress < 0 || block.endAddress < block.startAddress || block.quantity <= 0) {
            return QStringLiteral("扫描执行失败：第 %1 个请求块地址或数量无效。").arg(block.index + 1);
        }
    }

    return {};
}

bool shouldRetry(ScanAttemptStatus status, const ScanExecutionOptions& options) {
    switch (status) {
    case ScanAttemptStatus::Success:
        return false;
    case ScanAttemptStatus::Timeout:
        return options.retryOnTimeout;
    case ScanAttemptStatus::TransportError:
        return options.retryOnTransportError;
    case ScanAttemptStatus::ParseError:
        return options.retryOnParseError;
    case ScanAttemptStatus::ModbusException:
        return options.retryOnModbusException;
    }

    return false;
}

ScanAttemptResult attemptFromExchange(
    const ScanBlock& block,
    int attemptIndex,
    const QByteArray& fallbackRequestFrame,
    const ModbusTransportExchange& exchange) {
    ScanAttemptResult attempt;
    attempt.blockIndex = block.index;
    attempt.attemptIndex = attemptIndex;
    attempt.requestFrame = exchange.requestFrame.isEmpty() ? fallbackRequestFrame : exchange.requestFrame;
    attempt.responseFrame = exchange.responseFrame;
    attempt.errorMessage = exchange.errorMessage;
    attempt.sentAtUtc = exchange.sentAtUtc;
    attempt.receivedAtUtc = exchange.receivedAtUtc;
    attempt.endpoint = exchange.endpoint;

    switch (exchange.status) {
    case ModbusTransportStatus::Success:
        attempt.status = ScanAttemptStatus::ParseError;
        break;
    case ModbusTransportStatus::Timeout:
        attempt.status = ScanAttemptStatus::Timeout;
        if (attempt.errorMessage.isEmpty()) {
            attempt.errorMessage = QStringLiteral("等待 Modbus 响应超时。");
        }
        break;
    case ModbusTransportStatus::TransportError:
        attempt.status = ScanAttemptStatus::TransportError;
        if (attempt.errorMessage.isEmpty()) {
            attempt.errorMessage = QStringLiteral("Modbus 传输失败。");
        }
        break;
    }

    return attempt;
}

QDateTime bestObservedAtUtc(const ScanAttemptResult& attempt) {
    if (attempt.receivedAtUtc.isValid()) {
        return attempt.receivedAtUtc;
    }
    if (attempt.sentAtUtc.isValid()) {
        return attempt.sentAtUtc;
    }
    return QDateTime::currentDateTimeUtc();
}

bool isCancellationRequested(const ScanExecutionOptions& options) {
    return options.shouldCancel && options.shouldCancel();
}

} // namespace

QString describeScanAttemptStatus(ScanAttemptStatus status) {
    switch (status) {
    case ScanAttemptStatus::Success:
        return QStringLiteral("成功");
    case ScanAttemptStatus::ModbusException:
        return QStringLiteral("Modbus 异常");
    case ScanAttemptStatus::ParseError:
        return QStringLiteral("响应解析失败");
    case ScanAttemptStatus::Timeout:
        return QStringLiteral("响应超时");
    case ScanAttemptStatus::TransportError:
        return QStringLiteral("传输错误");
    }

    return QStringLiteral("未知状态");
}

QString describeScanExecutionStatus(ScanExecutionStatus status) {
    switch (status) {
    case ScanExecutionStatus::Completed:
        return QStringLiteral("已完成");
    case ScanExecutionStatus::CompletedWithErrors:
        return QStringLiteral("已完成但包含错误");
    case ScanExecutionStatus::Failed:
        return QStringLiteral("失败");
    }

    return QStringLiteral("未知状态");
}

ModbusScanExecutor::ModbusScanExecutor(ModbusRtuTransport& transport)
    : transport_(transport) {}

ScanExecutionResult ModbusScanExecutor::execute(const ScanPlan& plan, const ScanExecutionOptions& options) {
    ScanExecutionResult result;
    result.startedAtUtc = QDateTime::currentDateTimeUtc();
    result.plan = plan;

    const QString planError = invalidPlanMessage(plan, options);
    if (!planError.isEmpty()) {
        result.errorMessage = planError;
        result.finishedAtUtc = QDateTime::currentDateTimeUtc();
        return result;
    }

    result.blocks.reserve(plan.blocks.size());
    result.observations.reserve(plan.registerCount());
    bool cancelled = false;

    auto markCancelled = [&cancelled, &result]() {
        cancelled = true;
        result.errorMessage = QStringLiteral("扫描已取消。");
    };

    for (const auto& block : plan.blocks) {
        if (isCancellationRequested(options)) {
            markCancelled();
            break;
        }

        ScanBlockResult blockResult;
        blockResult.block = block;
        const int maxAttempts = plan.retryCount + 1;
        blockResult.attempts.reserve(maxAttempts);

        for (int attemptIndex = 0; attemptIndex < maxAttempts; ++attemptIndex) {
            if (isCancellationRequested(options)) {
                markCancelled();
                break;
            }

            const auto exchange = transport_.exchange(block.requestFrame, options.responseTimeoutMs);
            auto attempt = attemptFromExchange(block, attemptIndex, block.requestFrame, exchange);

            if (exchange.status == ModbusTransportStatus::Success) {
                const auto parsed = parseReadResponse(
                    exchange.responseFrame,
                    plan.slaveId,
                    plan.functionCode,
                    block.startAddress,
                    block.quantity);

                if (parsed.ok) {
                    attempt.status = ScanAttemptStatus::Success;
                    attempt.errorMessage.clear();
                    blockResult.ok = true;

                    const QDateTime observedAtUtc = bestObservedAtUtc(attempt);
                    blockResult.observations.reserve(parsed.observations.size());
                    for (const auto& observation : parsed.observations) {
                        blockResult.observations.append(ScanObservation{
                            block.index,
                            attemptIndex,
                            plan.slaveId,
                            plan.functionCode,
                            observation.address,
                            observation.value,
                            observedAtUtc
                        });
                    }
                } else if (parsed.isException) {
                    attempt.status = ScanAttemptStatus::ModbusException;
                    attempt.isModbusException = true;
                    attempt.exceptionCode = parsed.exceptionCode;
                    attempt.exceptionDescription = parsed.exceptionDescription;
                    attempt.errorMessage = parsed.errorMessage;
                } else {
                    attempt.status = ScanAttemptStatus::ParseError;
                    attempt.errorMessage = parsed.errorMessage;
                }
            }

            const bool success = attempt.status == ScanAttemptStatus::Success;
            const bool retryAllowed = attemptIndex + 1 < maxAttempts && shouldRetry(attempt.status, options);
            if (!success && attempt.errorMessage.isEmpty()) {
                attempt.errorMessage = describeScanAttemptStatus(attempt.status);
            }

            blockResult.finalErrorMessage = attempt.errorMessage;
            blockResult.attempts.append(attempt);

            if (success || !retryAllowed) {
                break;
            }
        }

        if (cancelled && blockResult.attempts.isEmpty()) {
            break;
        }

        if (blockResult.ok) {
            ++result.successBlockCount;
            result.observations += blockResult.observations;
        } else {
            ++result.failedBlockCount;
            if (result.errorMessage.isEmpty()) {
                result.errorMessage = blockResult.finalErrorMessage;
            }
        }

        result.blocks.append(blockResult);

        if (cancelled || (!blockResult.ok && !options.continueOnBlockError)) {
            break;
        }
    }

    if (cancelled) {
        result.status = result.successBlockCount > 0
            ? ScanExecutionStatus::CompletedWithErrors
            : ScanExecutionStatus::Failed;
    } else if (result.failedBlockCount == 0) {
        result.status = ScanExecutionStatus::Completed;
        result.errorMessage.clear();
    } else if (result.successBlockCount > 0) {
        result.status = ScanExecutionStatus::CompletedWithErrors;
    } else {
        result.status = ScanExecutionStatus::Failed;
    }

    result.finishedAtUtc = QDateTime::currentDateTimeUtc();
    return result;
}

} // namespace svm::modbus
