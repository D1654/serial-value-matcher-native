#pragma once

#include "capture/capture_bus.h"
#include "modbus/modbus_rtu_byte_channel.h"
#include "modbus/modbus_rtu_transport.h"

#include <QString>

namespace svm::modbus {

struct ModbusRtuSerialTransportOptions {
    QString sessionId;
    bool publishCaptureEvents = true;
};

class ModbusRtuSerialTransport final : public ModbusRtuTransport {
public:
    ModbusRtuSerialTransport(
        ModbusRtuByteChannel& channel,
        capture::CaptureBus* captureBus = nullptr,
        ModbusRtuSerialTransportOptions options = {});

    ModbusTransportExchange exchange(const QByteArray& requestFrame, int responseTimeoutMs) override;

private:
    void publishRawEvent(capture::Direction direction, const QByteArray& payload, const QDateTime& timestampUtc);

    ModbusRtuByteChannel& channel_;
    capture::CaptureBus* captureBus_ = nullptr;
    ModbusRtuSerialTransportOptions options_;
};

} // namespace svm::modbus
