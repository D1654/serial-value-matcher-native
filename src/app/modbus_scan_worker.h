#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "capture/raw_io_event.h"
#include "modbus/modbus_scan_executor.h"
#include "modbus/modbus_scan_plan.h"
#include "transport/serial_port_service.h"

namespace svm::app {

struct ModbusScanWorkerRequest {
    QString scanSessionId;
    transport::SerialOpenOptions serialOptions;
    modbus::ScanPlan plan;
    modbus::ScanExecutionOptions executionOptions;
};

class ModbusScanWorker final : public QObject {
    Q_OBJECT

public:
    ModbusScanWorker(
        ModbusScanWorkerRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        QObject* parent = nullptr);

public slots:
    void execute();

signals:
    void rawIoEventCaptured(svm::capture::RawIoEvent event);
    void finished(QString scanSessionId, svm::modbus::ScanExecutionResult result);
    void failed(QString message);

private:
    bool isCancellationRequested() const;
    svm::modbus::ScanExecutionResult cancelledResult() const;

    ModbusScanWorkerRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
};

} // namespace svm::app
