#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace svm::modbus {

enum class ModbusReadResponseKind {
    Success,
    CrcError,
    FrameTooShort,
    InvalidExpectedRequest,
    SlaveIdMismatch,
    ModbusException,
    ModbusExceptionLength,
    FunctionCodeMismatch,
    ByteCountMismatch,
    OddByteCount,
    RegisterQuantityMismatch,
    AddressRangeInvalid
};

struct RegisterObservation {
    quint16 address = 0;
    quint16 value = 0;
};

struct ParseReadResponseResult {
    bool ok = false;
    ModbusReadResponseKind kind = ModbusReadResponseKind::FrameTooShort;
    QString errorMessage;
    quint8 slaveId = 0;
    quint8 functionCode = 0;
    bool isException = false;
    quint8 exceptionCode = 0;
    QString exceptionDescription;
    QByteArray bodyWithoutCrc;
    QVector<quint16> registers;
    QVector<RegisterObservation> observations;
};

ParseReadResponseResult parseReadResponse(
    const QByteArray& frame,
    int expectedSlaveId,
    quint8 expectedFunctionCode,
    int expectedStartAddress,
    int expectedQuantity);

QString describeModbusException(quint8 exceptionCode);
QString describeModbusReadResponseKind(ModbusReadResponseKind kind);
bool isModbusReadResponseDataFormatFailure(ModbusReadResponseKind kind);

} // namespace svm::modbus
