#include "modbus/modbus_scan_executor.h"

#include "core/modbus_scan_executor_core.h"
#include "modbus/modbus_read_request.h"

#include <utility>

namespace svm::modbus {
namespace {

using CoreAttemptStatus = core::modbus::ScanAttemptStatus;
using CoreExecutionStatus = core::modbus::ScanExecutionStatus;
using CoreExchangeStatus = core::modbus::RtuTransportExchangeStatus;

QString fromCoreText(const core::Text& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

core::Text toCoreText(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return core::Text(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QByteArray fromCoreBytes(const core::ByteBuffer& bytes) {
    if (bytes.empty()) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

QByteArray fromCoreBytes(core::ByteSpan bytes) {
    if (bytes.empty()) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

core::ByteBuffer toCoreBytes(const QByteArray& bytes) {
    core::ByteBuffer result;
    result.reserve(static_cast<std::size_t>(bytes.size()));
    for (const char value : bytes) {
        result.push_back(static_cast<core::Byte>(static_cast<unsigned char>(value)));
    }
    return result;
}

core::ByteBuffer toCoreBytes(core::ByteSpan bytes) {
    return core::ByteBuffer(bytes.begin(), bytes.end());
}

core::Text toCoreTimestampText(const QDateTime& dateTime) {
    if (!dateTime.isValid()) {
        return {};
    }
    const QByteArray utf8 = dateTime.toUTC().toString(Qt::ISODateWithMs).toUtf8();
    return core::Text(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QDateTime fromCoreTimestampText(const core::Text& text) {
    if (text.empty()) {
        return {};
    }

    const QString value = fromCoreText(text);
    QDateTime dateTime = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(value, Qt::ISODate);
    }
    return dateTime.isValid() ? dateTime.toUTC() : QDateTime{};
}

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

core::modbus::ScanSafetyLevel toCoreSafetyLevel(ScanSafetyLevel level) {
    switch (level) {
    case ScanSafetyLevel::Conservative:
        return core::modbus::ScanSafetyLevel::Conservative;
    case ScanSafetyLevel::Balanced:
        return core::modbus::ScanSafetyLevel::Balanced;
    case ScanSafetyLevel::Aggressive:
        return core::modbus::ScanSafetyLevel::Aggressive;
    case ScanSafetyLevel::Custom:
        return core::modbus::ScanSafetyLevel::Custom;
    }

    return core::modbus::ScanSafetyLevel::Conservative;
}

core::modbus::ScanPlan toCorePlan(const ScanPlan& plan) {
    core::modbus::ScanPlan corePlan;
    corePlan.slaveId = plan.slaveId;
    corePlan.functionCode = plan.functionCode;
    corePlan.range = core::modbus::ScanRange{plan.range.startAddress, plan.range.endAddress};
    corePlan.blockSize = plan.blockSize;
    corePlan.requestIntervalMs = plan.requestIntervalMs;
    corePlan.retryCount = plan.retryCount;
    corePlan.safetyLevel = toCoreSafetyLevel(plan.safetyLevel);
    corePlan.blocks.reserve(static_cast<std::size_t>(plan.blocks.size()));
    for (const auto& block : plan.blocks) {
        corePlan.blocks.push_back(core::modbus::ScanBlock{
            block.index,
            block.startAddress,
            block.endAddress,
            block.quantity,
            toCoreBytes(block.requestFrame)
        });
    }
    return corePlan;
}

ScanBlock fromCoreBlock(const core::modbus::ScanBlock& block) {
    ScanBlock result;
    result.index = block.index;
    result.startAddress = block.startAddress;
    result.endAddress = block.endAddress;
    result.quantity = block.quantity;
    result.requestFrame = fromCoreBytes(block.requestFrame);
    return result;
}

CoreExchangeStatus toCoreExchangeStatus(ModbusTransportStatus status) {
    switch (status) {
    case ModbusTransportStatus::Success:
        return CoreExchangeStatus::Success;
    case ModbusTransportStatus::Timeout:
        return CoreExchangeStatus::Timeout;
    case ModbusTransportStatus::TransportError:
        return CoreExchangeStatus::TransportError;
    }

    return CoreExchangeStatus::TransportError;
}

ScanAttemptStatus fromCoreAttemptStatus(CoreAttemptStatus status) {
    switch (status) {
    case CoreAttemptStatus::Success:
        return ScanAttemptStatus::Success;
    case CoreAttemptStatus::ModbusException:
        return ScanAttemptStatus::ModbusException;
    case CoreAttemptStatus::ParseError:
        return ScanAttemptStatus::ParseError;
    case CoreAttemptStatus::Timeout:
        return ScanAttemptStatus::Timeout;
    case CoreAttemptStatus::TransportError:
        return ScanAttemptStatus::TransportError;
    }

    return ScanAttemptStatus::TransportError;
}

ScanExecutionStatus fromCoreExecutionStatus(CoreExecutionStatus status) {
    switch (status) {
    case CoreExecutionStatus::Completed:
        return ScanExecutionStatus::Completed;
    case CoreExecutionStatus::CompletedWithErrors:
        return ScanExecutionStatus::CompletedWithErrors;
    case CoreExecutionStatus::Failed:
        return ScanExecutionStatus::Failed;
    }

    return ScanExecutionStatus::Failed;
}

QString localizedExecutionError(const core::Text& errorMessage) {
    if (errorMessage == "Scan was cancelled.") {
        return QStringLiteral("扫描已取消。");
    }
    return fromCoreText(errorMessage);
}

class CoreTransportAdapter final : public core::modbus::RtuTransport {
public:
    explicit CoreTransportAdapter(ModbusRtuTransport& transport)
        : transport_(transport) {}

    core::modbus::RtuTransportExchange exchange(core::ByteSpan requestFrame, int responseTimeoutMs) override {
        const QByteArray qtRequest = fromCoreBytes(requestFrame);
        const ModbusTransportExchange qtExchange = transport_.exchange(qtRequest, responseTimeoutMs);

        core::modbus::RtuTransportExchange exchange;
        exchange.status = toCoreExchangeStatus(qtExchange.status);
        exchange.requestFrame = qtExchange.requestFrame.isEmpty() ? toCoreBytes(requestFrame) : toCoreBytes(qtExchange.requestFrame);
        exchange.responseFrame = toCoreBytes(qtExchange.responseFrame);
        exchange.errorMessage = toCoreText(qtExchange.errorMessage);
        exchange.sentAtUtc = toCoreTimestampText(qtExchange.sentAtUtc);
        exchange.receivedAtUtc = toCoreTimestampText(qtExchange.receivedAtUtc);
        exchange.endpoint = toCoreText(qtExchange.endpoint);
        return exchange;
    }

private:
    ModbusRtuTransport& transport_;
};

void localizeParseFields(ScanAttemptResult& attempt, const ScanPlan& plan, const ScanBlock& block) {
    if (attempt.responseFrame.isEmpty()
        || (attempt.status != ScanAttemptStatus::ParseError && attempt.status != ScanAttemptStatus::ModbusException)) {
        return;
    }

    const auto parsed = parseReadResponse(
        attempt.responseFrame,
        plan.slaveId,
        plan.functionCode,
        block.startAddress,
        block.quantity);
    if (parsed.ok) {
        return;
    }

    attempt.errorMessage = parsed.errorMessage;
    attempt.isModbusException = parsed.isException;
    attempt.exceptionCode = parsed.exceptionCode;
    attempt.exceptionDescription = parsed.exceptionDescription;
}

ScanAttemptResult fromCoreAttempt(
    const core::modbus::ScanAttemptResult& attempt,
    const ScanPlan& plan,
    const ScanBlock& block) {
    ScanAttemptResult result;
    result.blockIndex = attempt.blockIndex;
    result.attemptIndex = attempt.attemptIndex;
    result.status = fromCoreAttemptStatus(attempt.status);
    result.requestFrame = fromCoreBytes(attempt.requestFrame);
    result.responseFrame = fromCoreBytes(attempt.responseFrame);
    result.errorMessage = fromCoreText(attempt.errorMessage);
    result.isModbusException = attempt.isModbusException;
    result.exceptionCode = attempt.exceptionCode;
    result.exceptionDescription = fromCoreText(attempt.exceptionDescription);
    result.sentAtUtc = fromCoreTimestampText(attempt.sentAtUtc);
    result.receivedAtUtc = fromCoreTimestampText(attempt.receivedAtUtc);
    result.endpoint = fromCoreText(attempt.endpoint);
    localizeParseFields(result, plan, block);
    return result;
}

ScanObservation fromCoreObservation(const core::modbus::ScanObservation& observation) {
    ScanObservation result;
    result.blockIndex = observation.blockIndex;
    result.attemptIndex = observation.attemptIndex;
    result.slaveId = observation.slaveId;
    result.functionCode = observation.functionCode;
    result.address = observation.address;
    result.value = observation.value;
    result.observedAtUtc = fromCoreTimestampText(observation.observedAtUtc);
    return result;
}

ScanExecutionResult fromCoreResult(
    const ScanPlan& plan,
    const core::modbus::ScanExecutionResult& coreResult) {
    ScanExecutionResult result;
    result.status = fromCoreExecutionStatus(coreResult.status);
    result.errorMessage = localizedExecutionError(coreResult.errorMessage);
    result.startedAtUtc = fromCoreTimestampText(coreResult.startedAtUtc);
    result.finishedAtUtc = fromCoreTimestampText(coreResult.finishedAtUtc);
    result.plan = plan;
    result.successBlockCount = coreResult.successBlockCount;
    result.failedBlockCount = coreResult.failedBlockCount;
    result.blocks.reserve(static_cast<qsizetype>(coreResult.blocks.size()));
    result.observations.reserve(static_cast<qsizetype>(coreResult.observations.size()));

    for (const auto& coreBlock : coreResult.blocks) {
        ScanBlockResult block;
        block.block = fromCoreBlock(coreBlock.block);
        block.ok = coreBlock.ok;
        block.finalErrorMessage = fromCoreText(coreBlock.finalErrorMessage);
        block.attempts.reserve(static_cast<qsizetype>(coreBlock.attempts.size()));
        block.observations.reserve(static_cast<qsizetype>(coreBlock.observations.size()));

        for (const auto& coreAttempt : coreBlock.attempts) {
            block.attempts.append(fromCoreAttempt(coreAttempt, plan, block.block));
        }

        if (!block.ok && !block.attempts.isEmpty()) {
            block.finalErrorMessage = block.attempts.back().errorMessage;
        }

        for (const auto& coreObservation : coreBlock.observations) {
            block.observations.append(fromCoreObservation(coreObservation));
        }

        result.blocks.append(std::move(block));
    }

    for (const auto& coreObservation : coreResult.observations) {
        result.observations.append(fromCoreObservation(coreObservation));
    }

    if (!result.errorMessage.isEmpty()) {
        for (const auto& block : result.blocks) {
            if (!block.ok && !block.finalErrorMessage.isEmpty()) {
                result.errorMessage = block.finalErrorMessage;
                break;
            }
        }
    }

    return result;
}

core::modbus::ScanExecutionOptions toCoreOptions(const ScanExecutionOptions& options) {
    core::modbus::ScanExecutionOptions coreOptions;
    coreOptions.responseTimeoutMs = options.responseTimeoutMs;
    coreOptions.continueOnBlockError = options.continueOnBlockError;
    coreOptions.retryOnTimeout = options.retryOnTimeout;
    coreOptions.retryOnTransportError = options.retryOnTransportError;
    coreOptions.retryOnParseError = options.retryOnParseError;
    coreOptions.retryOnModbusException = options.retryOnModbusException;
    coreOptions.shouldCancel = options.shouldCancel;
    coreOptions.nowUtc = []() {
        return toCoreTimestampText(QDateTime::currentDateTimeUtc());
    };
    return coreOptions;
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

bool scanExecutionCompleted(ScanExecutionStatus status) noexcept {
    return status == ScanExecutionStatus::Completed;
}

bool scanExecutionTerminal(ScanExecutionStatus status) noexcept {
    switch (status) {
    case ScanExecutionStatus::Completed:
    case ScanExecutionStatus::CompletedWithErrors:
    case ScanExecutionStatus::Failed:
        return true;
    }
    return false;
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

    CoreTransportAdapter transportAdapter(transport_);
    core::modbus::ScanExecutor executor(transportAdapter);
    return fromCoreResult(plan, executor.execute(toCorePlan(plan), toCoreOptions(options)));
}

} // namespace svm::modbus
