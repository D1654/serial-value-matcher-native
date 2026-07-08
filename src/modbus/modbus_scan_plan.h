#pragma once

#include "modbus/modbus_read_request.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace svm::modbus {

enum class ScanSafetyLevel {
    Conservative,
    Balanced,
    Aggressive,
    Custom
};

enum class ScanPlanBuildStatus {
    Success,
    InvalidSlaveId,
    UnsupportedFunction,
    InvalidAddressRange,
    InvalidBlockSize,
    InvalidRequestInterval,
    InvalidRetryCount,
    PlanTooLarge,
    RequestBuildFailed
};

struct ScanRange {
    int startAddress = 0;
    int endAddress = 0;
};

struct ScanPlanOptions {
    int slaveId = 1;
    quint8 functionCode = static_cast<quint8>(ModbusReadFunction::HoldingRegisters);
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
    QByteArray requestFrame;
};

struct ScanPlan {
    int slaveId = 0;
    quint8 functionCode = 0;
    ScanRange range;
    int blockSize = 0;
    int requestIntervalMs = 0;
    int retryCount = 0;
    ScanSafetyLevel safetyLevel = ScanSafetyLevel::Conservative;
    QVector<ScanBlock> blocks;

    int registerCount() const;
    int requestCount() const;
    int estimatedAttemptCount() const;
    int estimatedInterRequestDelayMs() const;
};

struct BuildScanPlanResult {
    bool ok = false;
    ScanPlanBuildStatus status = ScanPlanBuildStatus::InvalidAddressRange;
    QString errorMessage;
    ScanPlan plan;
};

QString describeScanSafetyLevel(ScanSafetyLevel level);
QString describeScanPlanBuildStatus(ScanPlanBuildStatus status);
BuildScanPlanResult buildScanPlan(const ScanPlanOptions& options);

} // namespace svm::modbus
