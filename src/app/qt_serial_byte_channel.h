#pragma once

#include <QSerialPort>
#include <QString>

#include "modbus/modbus_rtu_byte_channel.h"
#include "transport/serial_port_service.h"

namespace svm::app {

class QtSerialByteChannel final : public modbus::ModbusRtuByteChannel {
public:
    explicit QtSerialByteChannel(transport::SerialOpenOptions options);
    ~QtSerialByteChannel() override;

    bool open();
    bool isOpen() const override;
    QString endpoint() const override;
    qint64 writeBytes(const QByteArray& payload) override;
    bool waitForReadyRead(int timeoutMs) override;
    QByteArray readAvailable() override;
    QString lastErrorText() const override;

private:
    transport::SerialOpenOptions options_;
    QSerialPort port_;
    QString lastErrorText_;
};

} // namespace svm::app
