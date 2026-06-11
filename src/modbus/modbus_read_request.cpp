#include "modbus/modbus_read_request.h"

#include "modbus/modbus_rtu_codec.h"

#include <utility>

namespace svm::modbus {
namespace {

constexpr int MinimumSlaveId = 1;
constexpr int MaximumSlaveId = 247;
constexpr int MinimumRegisterAddress = 0;
constexpr int MaximumRegisterAddress = 0xFFFF;
constexpr int MinimumQuantity = 1;
constexpr int MaximumReadQuantity = 125;

BuildReadRequestResult fail(QString message) {
    BuildReadRequestResult result;
    result.errorMessage = std::move(message);
    return result;
}

void appendUInt16BigEndian(QByteArray& data, int value) {
    data.append(static_cast<char>((value >> 8) & 0xFF));
    data.append(static_cast<char>(value & 0xFF));
}

} // namespace

bool isSupportedReadFunction(quint8 functionCode) {
    return functionCode == static_cast<quint8>(ModbusReadFunction::HoldingRegisters)
        || functionCode == static_cast<quint8>(ModbusReadFunction::InputRegisters);
}

QString describeReadFunction(quint8 functionCode) {
    switch (functionCode) {
    case static_cast<quint8>(ModbusReadFunction::HoldingRegisters):
        return QStringLiteral("FC03 保持寄存器（只读）");
    case static_cast<quint8>(ModbusReadFunction::InputRegisters):
        return QStringLiteral("FC04 输入寄存器（只读）");
    default: {
        const QString digits = QString::number(functionCode, 16).rightJustified(2, QLatin1Char('0')).toUpper();
        return QStringLiteral("不支持的功能码 0x%1").arg(digits);
    }
    }
}

BuildReadRequestResult buildReadRequest(int slaveId, ModbusReadFunction function, int startAddress, int quantity) {
    return buildReadRequest(slaveId, static_cast<quint8>(function), startAddress, quantity);
}

BuildReadRequestResult buildReadRequest(int slaveId, quint8 functionCode, int startAddress, int quantity) {
    if (slaveId < MinimumSlaveId || slaveId > MaximumSlaveId) {
        return fail(QStringLiteral("从站 ID 无效：必须是 1-247，不能使用广播地址 0。"));
    }

    if (!isSupportedReadFunction(functionCode)) {
        return fail(QStringLiteral("功能码无效：只允许 FC03/FC04 只读请求，当前为 %1。")
            .arg(describeReadFunction(functionCode)));
    }

    if (startAddress < MinimumRegisterAddress || startAddress > MaximumRegisterAddress) {
        return fail(QStringLiteral("起始地址无效：必须在 0-65535 范围内。"));
    }

    if (quantity < MinimumQuantity || quantity > MaximumReadQuantity) {
        return fail(QStringLiteral("读取数量无效：每个请求必须读取 1-125 个寄存器。"));
    }

    if (startAddress + quantity - 1 > MaximumRegisterAddress) {
        return fail(QStringLiteral("地址范围无效：起始地址加读取数量超出 65535。"));
    }

    QByteArray body;
    body.reserve(6);
    body.append(static_cast<char>(slaveId));
    body.append(static_cast<char>(functionCode));
    appendUInt16BigEndian(body, startAddress);
    appendUInt16BigEndian(body, quantity);

    BuildReadRequestResult result;
    result.ok = true;
    result.frame = appendCrc16Modbus(body);
    return result;
}

} // namespace svm::modbus
