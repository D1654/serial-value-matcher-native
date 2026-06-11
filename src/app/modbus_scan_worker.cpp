#include "app/modbus_scan_worker.h"

#include <QMetaType>

#include <utility>

#include "app/qt_serial_byte_channel.h"
#include "capture/capture_bus.h"
#include "modbus/modbus_rtu_serial_transport.h"

namespace svm::app {

ModbusScanWorker::ModbusScanWorker(
    ModbusScanWorkerRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested,
    QObject* parent)
    : QObject(parent),
      request_(std::move(request)),
      cancelRequested_(std::move(cancelRequested))
{
    qRegisterMetaType<svm::capture::RawIoEvent>("svm::capture::RawIoEvent");
    qRegisterMetaType<svm::modbus::ScanExecutionResult>("svm::modbus::ScanExecutionResult");
}

void ModbusScanWorker::execute()
{
    if (isCancellationRequested()) {
        emit finished(request_.scanSessionId, cancelledResult());
        return;
    }

    QtSerialByteChannel channel(request_.serialOptions);
    if (!channel.open()) {
        emit failed(channel.lastErrorText());
        return;
    }

    capture::CaptureBus captureBus;
    connect(&captureBus, &capture::CaptureBus::eventCaptured, this, &ModbusScanWorker::rawIoEventCaptured);

    modbus::ModbusRtuSerialTransport transport(
        channel,
        &captureBus,
        modbus::ModbusRtuSerialTransportOptions{request_.scanSessionId, true});
    modbus::ModbusScanExecutor executor(transport);

    auto executionOptions = request_.executionOptions;
    executionOptions.shouldCancel = [this]() {
        return isCancellationRequested();
    };
    emit finished(request_.scanSessionId, executor.execute(request_.plan, executionOptions));
}

bool ModbusScanWorker::isCancellationRequested() const
{
    return cancelRequested_ != nullptr && cancelRequested_->load(std::memory_order_relaxed);
}

modbus::ScanExecutionResult ModbusScanWorker::cancelledResult() const
{
    modbus::ScanExecutionResult result;
    result.status = modbus::ScanExecutionStatus::Failed;
    result.errorMessage = QStringLiteral("扫描已取消。");
    result.startedAtUtc = QDateTime::currentDateTimeUtc();
    result.finishedAtUtc = result.startedAtUtc;
    result.plan = request_.plan;
    return result;
}

} // namespace svm::app
