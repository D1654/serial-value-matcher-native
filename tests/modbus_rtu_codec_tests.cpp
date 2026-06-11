#include <QtTest/QtTest>

#include "modbus/modbus_rtu_codec.h"

class ModbusRtuCodecTests final : public QObject {
    Q_OBJECT

private slots:
    void crc16ModbusKnownVector() {
        const QByteArray body = QByteArray::fromHex("01030000000A");

        QCOMPARE(svm::modbus::crc16Modbus(body), static_cast<quint16>(0xCDC5));
    }

    void appendsCrcAsLittleEndianBytes() {
        const QByteArray body = QByteArray::fromHex("0103006B0003");

        QCOMPARE(svm::modbus::appendCrc16Modbus(body), QByteArray::fromHex("0103006B00037417"));
    }

    void validatesRtuFrameAndReturnsBodyWithoutCrc() {
        const QByteArray frame = QByteArray::fromHex("01030000000AC5CD");

        const auto check = svm::modbus::validateRtuFrame(frame);

        QVERIFY2(check.ok, qPrintable(check.errorMessage));
        QCOMPARE(check.bodyWithoutCrc, QByteArray::fromHex("01030000000A"));
        QCOMPARE(check.actualCrc, static_cast<quint16>(0xCDC5));
        QCOMPARE(check.expectedCrc, static_cast<quint16>(0xCDC5));
    }

    void rejectsTooShortFrameWithChineseReason() {
        const auto check = svm::modbus::validateRtuFrame(QByteArray::fromHex("0103"));

        QVERIFY(!check.ok);
        QVERIFY(check.errorMessage.contains(QStringLiteral("太短")));
    }

    void rejectsCrcMismatchWithChineseReason() {
        const auto check = svm::modbus::validateRtuFrame(QByteArray::fromHex("01030000000A0000"));

        QVERIFY(!check.ok);
        QVERIFY(check.errorMessage.contains(QStringLiteral("CRC")));
        QCOMPARE(check.actualCrc, static_cast<quint16>(0x0000));
        QCOMPARE(check.expectedCrc, static_cast<quint16>(0xCDC5));
    }

    void formatsCrc16ForDiagnostics() {
        QCOMPARE(svm::modbus::formatCrc16(0x00AF), QStringLiteral("0x00AF"));
    }
};

QTEST_MAIN(ModbusRtuCodecTests)
#include "modbus_rtu_codec_tests.moc"
