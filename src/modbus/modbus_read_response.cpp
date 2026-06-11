#include "modbus/modbus_read_response.h"

#include "modbus/modbus_read_request.h"
#include "modbus/modbus_rtu_codec.h"

#include <utility>

namespace svm::modbus {
namespace {

ParseReadResponseResult fail(QString message) {
    ParseReadResponseResult result;
    result.errorMessage = std::move(message);
    return result;
}

quint8 byteAt(const QByteArray& data, int index) {
    return static_cast<quint8>(data.at(index));
}

quint16 readUInt16BigEndian(const QByteArray& data, int offset) {
    return static_cast<quint16>((static_cast<quint16>(byteAt(data, offset)) << 8) | byteAt(data, offset + 1));
}

} // namespace

ParseReadResponseResult parseReadResponse(
    const QByteArray& frame,
    int expectedSlaveId,
    quint8 expectedFunctionCode,
    int expectedStartAddress,
    int expectedQuantity) {
    const auto frameCheck = validateRtuFrame(frame);
    if (!frameCheck.ok) {
        return fail(frameCheck.errorMessage);
    }

    const QByteArray& body = frameCheck.bodyWithoutCrc;
    if (body.size() < 3) {
        return fail(QStringLiteral("Modbus 响应太短：至少需要从站地址、功能码和数据长度或异常码。"));
    }

    ParseReadResponseResult result;
    result.bodyWithoutCrc = body;
    result.slaveId = byteAt(body, 0);
    result.functionCode = byteAt(body, 1);

    if (expectedSlaveId < 1 || expectedSlaveId > 247) {
        return fail(QStringLiteral("期望从站 ID 无效：必须是 1-247。"));
    }

    if (!isSupportedReadFunction(expectedFunctionCode)) {
        return fail(QStringLiteral("期望功能码无效：只支持 FC03/FC04。"));
    }

    if (expectedStartAddress < 0 || expectedStartAddress > 0xFFFF || expectedQuantity < 1) {
        return fail(QStringLiteral("期望地址或寄存器数量无效。"));
    }

    if (result.slaveId != static_cast<quint8>(expectedSlaveId)) {
        return fail(QStringLiteral("从站 ID 不匹配：响应来自 %1，期望 %2。")
            .arg(result.slaveId)
            .arg(expectedSlaveId));
    }

    const quint8 expectedExceptionFunction = static_cast<quint8>(expectedFunctionCode | 0x80u);
    if (result.functionCode == expectedExceptionFunction) {
        if (body.size() != 3) {
            return fail(QStringLiteral("Modbus 异常响应长度无效：异常响应应包含从站、功能码和异常码。"));
        }
        result.isException = true;
        result.exceptionCode = byteAt(body, 2);
        result.exceptionDescription = describeModbusException(result.exceptionCode);
        result.errorMessage = QStringLiteral("设备返回 Modbus 异常：%1。").arg(result.exceptionDescription);
        return result;
    }

    if (result.functionCode != expectedFunctionCode) {
        return fail(QStringLiteral("功能码不匹配：响应为 0x%1，期望 0x%2。")
            .arg(result.functionCode, 2, 16, QLatin1Char('0'))
            .arg(expectedFunctionCode, 2, 16, QLatin1Char('0')));
    }

    const int byteCount = byteAt(body, 2);
    const int payloadSize = body.size() - 3;
    if (byteCount != payloadSize) {
        return fail(QStringLiteral("响应字节数不匹配：声明 %1 字节，实际 %2 字节。")
            .arg(byteCount)
            .arg(payloadSize));
    }

    if (byteCount % 2 != 0) {
        return fail(QStringLiteral("响应字节数无效：寄存器数据必须是偶数字节。"));
    }

    const int registerCount = byteCount / 2;
    if (registerCount != expectedQuantity) {
        return fail(QStringLiteral("响应寄存器数量不匹配：解析到 %1 个，期望 %2 个。")
            .arg(registerCount)
            .arg(expectedQuantity));
    }

    if (expectedStartAddress + registerCount - 1 > 0xFFFF) {
        return fail(QStringLiteral("响应地址范围无效：起始地址加寄存器数量超出 65535。"));
    }

    result.registers.reserve(registerCount);
    result.observations.reserve(registerCount);
    for (int index = 0; index < registerCount; ++index) {
        const quint16 value = readUInt16BigEndian(body, 3 + index * 2);
        result.registers.append(value);
        result.observations.append(RegisterObservation{
            static_cast<quint16>(expectedStartAddress + index),
            value
        });
    }

    result.ok = true;
    return result;
}

QString describeModbusException(quint8 exceptionCode) {
    switch (exceptionCode) {
    case 0x01:
        return QStringLiteral("非法功能码");
    case 0x02:
        return QStringLiteral("非法数据地址");
    case 0x03:
        return QStringLiteral("非法数据值");
    case 0x04:
        return QStringLiteral("从站设备故障");
    case 0x05:
        return QStringLiteral("确认，设备需要较长时间处理");
    case 0x06:
        return QStringLiteral("从站设备忙");
    default: {
        const QString digits = QString::number(exceptionCode, 16).rightJustified(2, QLatin1Char('0')).toUpper();
        return QStringLiteral("未知异常码 0x%1").arg(digits);
    }
    }
}

} // namespace svm::modbus
