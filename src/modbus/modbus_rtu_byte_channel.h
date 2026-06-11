#pragma once

#include <QByteArray>
#include <QString>

namespace svm::modbus {

class ModbusRtuByteChannel {
public:
    virtual ~ModbusRtuByteChannel() = default;

    virtual bool isOpen() const = 0;
    virtual QString endpoint() const = 0;
    virtual QString lastErrorText() const = 0;
    virtual qint64 writeBytes(const QByteArray& payload) = 0;
    virtual bool waitForReadyRead(int timeoutMs) = 0;
    virtual QByteArray readAvailable() = 0;
};

} // namespace svm::modbus
