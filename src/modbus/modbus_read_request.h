#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace svm::modbus {

enum class ModbusReadFunction : quint8 {
    HoldingRegisters = 0x03,
    InputRegisters = 0x04
};

struct BuildReadRequestResult {
    bool ok = false;
    QString errorMessage;
    QByteArray frame;
};

bool isSupportedReadFunction(quint8 functionCode);
QString describeReadFunction(quint8 functionCode);
BuildReadRequestResult buildReadRequest(int slaveId, ModbusReadFunction function, int startAddress, int quantity);
BuildReadRequestResult buildReadRequest(int slaveId, quint8 functionCode, int startAddress, int quantity);

} // namespace svm::modbus
