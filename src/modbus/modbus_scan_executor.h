#pragma once

#include "modbus/modbus_read_response.h"
#include "modbus/modbus_rtu_transport.h"
#include "modbus/modbus_scan_plan.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <functional>

namespace svm::modbus {

enum class ScanAttemptStatus {
    Success,
    ModbusException,
    ParseError,
    Timeout,
    TransportError
};

enum class ScanExecutionStatus {
    Completed,
    CompletedWithErrors,
    Failed
};

struct ScanAttemptResult {
    int blockIndex = -1;
    int attemptIndex = 0;
    ScanAttemptStatus status = ScanAttemptStatus::TransportError;
    QByteArray requestFrame;
    QByteArray responseFrame;
    QString errorMessage;
    bool isModbusException = false;
    quint8 exceptionCode = 0;
    QString exceptionDescription;
    QDateTime sentAtUtc;
    QDateTime receivedAtUtc;
    QString endpoint;
};

struct ScanObservation {
    int blockIndex = -1;
    int attemptIndex = 0;
    int slaveId = 0;
    quint8 functionCode = 0;
    quint16 address = 0;
    quint16 value = 0;
    QDateTime observedAtUtc;
};

struct ScanBlockResult {
    ScanBlock block;
    QVector<ScanAttemptResult> attempts;
    QVector<ScanObservation> observations;
    bool ok = false;
    QString finalErrorMessage;
};

struct ScanExecutionOptions {
    int responseTimeoutMs = 1000;
    bool continueOnBlockError = true;
    bool retryOnTimeout = true;
    bool retryOnTransportError = true;
    bool retryOnParseError = false;
    bool retryOnModbusException = false;
    std::function<bool()> shouldCancel;
};

struct ScanExecutionResult {
    ScanExecutionStatus status = ScanExecutionStatus::Failed;
    QString errorMessage;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    ScanPlan plan;
    QVector<ScanBlockResult> blocks;
    QVector<ScanObservation> observations;
    int successBlockCount = 0;
    int failedBlockCount = 0;
};

QString describeScanAttemptStatus(ScanAttemptStatus status);
QString describeScanExecutionStatus(ScanExecutionStatus status);
bool scanExecutionCompleted(ScanExecutionStatus status) noexcept;
bool scanExecutionTerminal(ScanExecutionStatus status) noexcept;

class ModbusScanExecutor {
public:
    explicit ModbusScanExecutor(ModbusRtuTransport& transport);

    ScanExecutionResult execute(const ScanPlan& plan, const ScanExecutionOptions& options = {});

private:
    ModbusRtuTransport& transport_;
};

} // namespace svm::modbus

Q_DECLARE_METATYPE(svm::modbus::ScanExecutionResult)
