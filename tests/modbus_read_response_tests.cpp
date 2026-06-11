#include <QtTest/QtTest>

#include "modbus/modbus_read_response.h"

class ModbusReadResponseTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesNormalFc03ResponseIntoRegistersAndObservations() {
        const QByteArray frame = QByteArray::fromHex("010306022B00000064057A");

        const auto result = svm::modbus::parseReadResponse(frame, 1, 0x03, 0x006B, 3);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.slaveId, static_cast<quint8>(1));
        QCOMPARE(result.functionCode, static_cast<quint8>(0x03));
        QCOMPARE(result.registers.size(), 3);
        QCOMPARE(result.registers[0], static_cast<quint16>(0x022B));
        QCOMPARE(result.registers[1], static_cast<quint16>(0x0000));
        QCOMPARE(result.registers[2], static_cast<quint16>(0x0064));
        QCOMPARE(result.observations.size(), 3);
        QCOMPARE(result.observations[0].address, static_cast<quint16>(0x006B));
        QCOMPARE(result.observations[0].value, static_cast<quint16>(0x022B));
        QCOMPARE(result.observations[2].address, static_cast<quint16>(0x006D));
        QCOMPARE(result.observations[2].value, static_cast<quint16>(0x0064));
    }

    void parsesNormalFc04Response() {
        const QByteArray frame = QByteArray::fromHex("1104060001000200037152");

        const auto result = svm::modbus::parseReadResponse(frame, 0x11, 0x04, 0, 3);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.registers, QVector<quint16>({1, 2, 3}));
    }

    void parsesExceptionResponseAsStructuredFailure() {
        const QByteArray frame = QByteArray::fromHex("018302C0F1");

        const auto result = svm::modbus::parseReadResponse(frame, 1, 0x03, 0, 1);

        QVERIFY(!result.ok);
        QVERIFY(result.isException);
        QCOMPARE(result.exceptionCode, static_cast<quint8>(0x02));
        QCOMPARE(result.exceptionDescription, QStringLiteral("非法数据地址"));
        QVERIFY(result.errorMessage.contains(QStringLiteral("Modbus 异常")));
    }

    void rejectsCrcMismatch() {
        const auto result = svm::modbus::parseReadResponse(QByteArray::fromHex("010306022B000000640000"), 1, 0x03, 0x006B, 3);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("CRC")));
    }

    void rejectsMismatchedSlaveId() {
        const auto result = svm::modbus::parseReadResponse(QByteArray::fromHex("010306022B00000064057A"), 2, 0x03, 0x006B, 3);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("从站 ID 不匹配")));
    }

    void rejectsMismatchedFunctionCode() {
        const auto result = svm::modbus::parseReadResponse(QByteArray::fromHex("010306022B00000064057A"), 1, 0x04, 0x006B, 3);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("功能码不匹配")));
    }

    void rejectsByteCountMismatch() {
        const auto result = svm::modbus::parseReadResponse(QByteArray::fromHex("010304022B0000006426BA"), 1, 0x03, 0x006B, 3);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("字节数不匹配")));
    }

    void rejectsRegisterQuantityMismatch() {
        const auto result = svm::modbus::parseReadResponse(QByteArray::fromHex("010306022B00000064057A"), 1, 0x03, 0x006B, 2);

        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("寄存器数量不匹配")));
    }

    void describesCommonExceptionCodesInChinese() {
        QCOMPARE(svm::modbus::describeModbusException(0x01), QStringLiteral("非法功能码"));
        QCOMPARE(svm::modbus::describeModbusException(0x02), QStringLiteral("非法数据地址"));
        QCOMPARE(svm::modbus::describeModbusException(0x06), QStringLiteral("从站设备忙"));
        QCOMPARE(svm::modbus::describeModbusException(0x22), QStringLiteral("未知异常码 0x22"));
    }
};

QTEST_MAIN(ModbusReadResponseTests)
#include "modbus_read_response_tests.moc"
