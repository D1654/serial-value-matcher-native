#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace svm::modbus {

struct RtuFrameCheck {
    bool ok = false;
    QString errorMessage;
    QByteArray bodyWithoutCrc;
    quint16 actualCrc = 0;
    quint16 expectedCrc = 0;
};

quint16 crc16Modbus(const QByteArray& data);
QByteArray appendCrc16Modbus(const QByteArray& bodyWithoutCrc);
RtuFrameCheck validateRtuFrame(const QByteArray& frame);
QByteArray bodyWithoutCrc(const QByteArray& frame);
quint16 readFrameCrcLittleEndian(const QByteArray& frame);
QString formatCrc16(quint16 value);

} // namespace svm::modbus
