#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace svm::modbus {

enum class ModbusTransportStatus {
    Success,
    Timeout,
    TransportError
};

struct ModbusTransportExchange {
    ModbusTransportStatus status = ModbusTransportStatus::TransportError;
    QByteArray requestFrame;
    QByteArray responseFrame;
    QString errorMessage;
    QDateTime sentAtUtc;
    QDateTime receivedAtUtc;
    QString endpoint;
};

class ModbusRtuTransport {
public:
    virtual ~ModbusRtuTransport() = default;

    virtual ModbusTransportExchange exchange(const QByteArray& requestFrame, int responseTimeoutMs) = 0;
};

} // namespace svm::modbus
