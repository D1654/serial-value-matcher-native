#include <QtTest/QtTest>

#include "protocol/checksum.h"

class ChecksumTests final : public QObject {
    Q_OBJECT

private slots:
    void sum8_knownVector() {
        QCOMPARE(svm::protocol::sum8(QByteArray::fromHex("01020304")), static_cast<quint8>(0x0A));
    }

    void xor8_knownVector() {
        QCOMPARE(svm::protocol::xor8(QByteArray::fromHex("01020304")), static_cast<quint8>(0x04));
    }

    void lrc8_knownVector() {
        QCOMPARE(svm::protocol::lrc8(QByteArray::fromHex("01020304")), static_cast<quint8>(0xF6));
    }
};

QTEST_MAIN(ChecksumTests)
#include "checksum_tests.moc"
