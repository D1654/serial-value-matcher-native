#include "modbus/modbus_rtu_codec.h"

namespace svm::modbus {
namespace {

constexpr int MinimumRtuFrameSize = 4; // slave id + function code + CRC16(LE)

quint8 byteAt(const QByteArray& data, int index) {
    return static_cast<quint8>(data.at(index));
}

} // namespace

quint16 crc16Modbus(const QByteArray& data) {
    quint16 crc = 0xFFFF;
    for (const char rawByte : data) {
        crc ^= static_cast<quint8>(rawByte);
        for (int bit = 0; bit < 8; ++bit) {
            const bool lsbSet = (crc & 0x0001u) != 0;
            crc >>= 1;
            if (lsbSet) {
                crc ^= 0xA001u;
            }
        }
    }
    return crc;
}

QByteArray appendCrc16Modbus(const QByteArray& bodyWithoutCrc) {
    QByteArray frame = bodyWithoutCrc;
    const quint16 crc = crc16Modbus(bodyWithoutCrc);
    frame.append(static_cast<char>(crc & 0x00FFu));
    frame.append(static_cast<char>((crc >> 8) & 0x00FFu));
    return frame;
}

RtuFrameCheck validateRtuFrame(const QByteArray& frame) {
    RtuFrameCheck result;
    if (frame.size() < MinimumRtuFrameSize) {
        result.errorMessage = QStringLiteral("RTU 帧太短：至少需要从站地址、功能码和 2 字节 CRC。");
        return result;
    }

    result.bodyWithoutCrc = bodyWithoutCrc(frame);
    result.actualCrc = readFrameCrcLittleEndian(frame);
    result.expectedCrc = crc16Modbus(result.bodyWithoutCrc);
    if (result.actualCrc != result.expectedCrc) {
        result.errorMessage = QStringLiteral("RTU CRC 校验失败：帧内 CRC 为 %1，应为 %2。")
            .arg(formatCrc16(result.actualCrc), formatCrc16(result.expectedCrc));
        return result;
    }

    result.ok = true;
    return result;
}

QByteArray bodyWithoutCrc(const QByteArray& frame) {
    if (frame.size() <= 2) {
        return {};
    }
    return frame.left(frame.size() - 2);
}

quint16 readFrameCrcLittleEndian(const QByteArray& frame) {
    if (frame.size() < 2) {
        return 0;
    }

    const int lowIndex = frame.size() - 2;
    const int highIndex = frame.size() - 1;
    return static_cast<quint16>(byteAt(frame, lowIndex) | (static_cast<quint16>(byteAt(frame, highIndex)) << 8));
}

QString formatCrc16(quint16 value) {
    const QString digits = QString::number(value, 16).rightJustified(4, QLatin1Char('0')).toUpper();
    return QStringLiteral("0x%1").arg(digits);
}

} // namespace svm::modbus
