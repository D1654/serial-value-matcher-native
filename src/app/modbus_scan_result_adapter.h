#pragma once

#include <QString>

#include "modbus/modbus_scan_executor.h"
#include "storage/scan_persistence_records.h"

namespace svm::app {

storage::ScanExecutionPersistenceRecord scanExecutionToPersistence(
    const QString& sessionId,
    const modbus::ScanExecutionResult& result);

QString scanSummaryText(const QString& sessionId, const modbus::ScanExecutionResult& result);

} // namespace svm::app
