#include <QtTest/QtTest>

#include "modbus/modbus_read_request.h"

class ModbusReadRequestTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsFc03ReadHoldingRegistersRequest() {
        const auto result = svm::modbus::buildReadRequest(
            1,
            svm::modbus::ModbusReadFunction::HoldingRegisters,
            0x006B,
            3);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.frame, QByteArray::fromHex("0103006B00037417"));
    }

    void buildsFc04ReadInputRegistersRequest() {
        const auto result = svm::modbus::buildReadRequest(
            0x11,
            svm::modbus::ModbusReadFunction::InputRegisters,
            0x006B,
            3);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.frame, QByteArray::fromHex("1104006B0003C347"));
    }

    void rejectsBroadcastSlaveIdZero() {
        const auto result = svm::modbus::buildReadRequest(
            0,
            svm::modbus::ModbusReadFunction::HoldingRegisters,
            0,
            1);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("广播地址 0")));
    }

    void rejectsUnsupportedFunctionCode() {
        const auto result = svm::modbus::buildReadRequest(1, static_cast<quint8>(0x06), 0, 1);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("只允许 FC03/FC04")));
        QVERIFY(result.errorMessage.contains(QStringLiteral("0x06")));
    }

    void rejectsInvalidQuantity() {
        const auto zero = svm::modbus::buildReadRequest(1, svm::modbus::ModbusReadFunction::HoldingRegisters, 0, 0);
        const auto tooMany = svm::modbus::buildReadRequest(1, svm::modbus::ModbusReadFunction::HoldingRegisters, 0, 126);

        QVERIFY(!zero.ok);
        QVERIFY(!tooMany.ok);
        QVERIFY(zero.errorMessage.contains(QStringLiteral("1-125")));
        QVERIFY(tooMany.errorMessage.contains(QStringLiteral("1-125")));
    }

    void rejectsAddressOverflow() {
        const auto result = svm::modbus::buildReadRequest(
            1,
            svm::modbus::ModbusReadFunction::InputRegisters,
            0xFFFF,
            2);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("超出 65535")));
    }

    void describesReadFunctionsInChinese() {
        QCOMPARE(svm::modbus::describeReadFunction(0x03), QStringLiteral("FC03 保持寄存器（只读）"));
        QCOMPARE(svm::modbus::describeReadFunction(0x04), QStringLiteral("FC04 输入寄存器（只读）"));
        QCOMPARE(svm::modbus::describeReadFunction(0x10), QStringLiteral("不支持的功能码 0x10"));
    }
};

QTEST_MAIN(ModbusReadRequestTests)
#include "modbus_read_request_tests.moc"
