#include "app/modbus_scan_result_adapter.h"

namespace svm::app {

namespace {

QString scanExecutionStatusName(modbus::ScanExecutionStatus status)
{
    switch (status) {
    case modbus::ScanExecutionStatus::Completed:
        return QStringLiteral("Completed");
    case modbus::ScanExecutionStatus::CompletedWithErrors:
        return QStringLiteral("CompletedWithErrors");
    case modbus::ScanExecutionStatus::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QString scanAttemptStatusName(modbus::ScanAttemptStatus status)
{
    switch (status) {
    case modbus::ScanAttemptStatus::Success:
        return QStringLiteral("Success");
    case modbus::ScanAttemptStatus::ModbusException:
        return QStringLiteral("ModbusException");
    case modbus::ScanAttemptStatus::ParseError:
        return QStringLiteral("ParseError");
    case modbus::ScanAttemptStatus::Timeout:
        return QStringLiteral("Timeout");
    case modbus::ScanAttemptStatus::TransportError:
        return QStringLiteral("TransportError");
    }
    return QStringLiteral("Unknown");
}

} // namespace

storage::ScanExecutionPersistenceRecord scanExecutionToPersistence(
    const QString& sessionId,
    const modbus::ScanExecutionResult& result)
{
    storage::ScanExecutionPersistenceRecord persistence;
    persistence.session.sessionId = sessionId;
    persistence.session.slaveId = result.plan.slaveId;
    persistence.session.functionCode = result.plan.functionCode;
    persistence.session.startAddress = result.plan.range.startAddress;
    persistence.session.endAddress = result.plan.range.endAddress;
    persistence.session.blockSize = result.plan.blockSize;
    persistence.session.requestCount = result.plan.requestCount();
    persistence.session.status = scanExecutionStatusName(result.status);
    persistence.session.startedAtUtc = result.startedAtUtc;
    persistence.session.finishedAtUtc = result.finishedAtUtc;
    persistence.session.successBlockCount = result.successBlockCount;
    persistence.session.failedBlockCount = result.failedBlockCount;
    persistence.session.errorMessage = result.errorMessage;

    for (const auto& block : result.blocks) {
        for (const auto& attempt : block.attempts) {
            storage::ScanAttemptRecord record;
            record.sessionId = sessionId;
            record.blockIndex = attempt.blockIndex;
            record.attemptIndex = attempt.attemptIndex;
            record.startAddress = block.block.startAddress;
            record.quantity = block.block.quantity;
            record.status = scanAttemptStatusName(attempt.status);
            record.requestFrame = attempt.requestFrame;
            record.responseFrame = attempt.responseFrame;
            record.errorMessage = attempt.errorMessage;
            record.isModbusException = attempt.isModbusException;
            record.exceptionCode = attempt.exceptionCode;
            record.exceptionDescription = attempt.exceptionDescription;
            record.sentAtUtc = attempt.sentAtUtc;
            record.receivedAtUtc = attempt.receivedAtUtc;
            record.endpoint = attempt.endpoint;
            persistence.attempts.append(record);
        }

        for (const auto& observation : block.observations) {
            storage::ScanObservationRecord record;
            record.sessionId = sessionId;
            record.blockIndex = observation.blockIndex;
            record.attemptIndex = observation.attemptIndex;
            record.slaveId = observation.slaveId;
            record.functionCode = observation.functionCode;
            record.address = observation.address;
            record.value = observation.value;
            record.observedAtUtc = observation.observedAtUtc;
            persistence.observations.append(record);
        }
    }

    return persistence;
}

QString scanSummaryText(const QString& sessionId, const modbus::ScanExecutionResult& result)
{
    int attemptCount = 0;
    for (const auto& block : result.blocks) {
        attemptCount += block.attempts.size();
    }

    return QStringLiteral("扫描会话：%1｜状态：%2｜请求块：%3｜尝试：%4｜成功块：%5｜失败块：%6｜寄存器观测：%7")
        .arg(sessionId)
        .arg(modbus::describeScanExecutionStatus(result.status))
        .arg(result.plan.requestCount())
        .arg(attemptCount)
        .arg(result.successBlockCount)
        .arg(result.failedBlockCount)
        .arg(result.observations.size());
}

} // namespace svm::app
