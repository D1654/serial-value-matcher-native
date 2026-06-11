#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "storage/session_store.h"

class SerialProfileTests final : public QObject {
    Q_OBJECT

private slots:
    void savesAndLoadsLatestProfile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("profiles.sqlite"))), qPrintable(store.lastErrorText()));

        svm::storage::SerialProfile profile;
        profile.name = QStringLiteral("default");
        profile.options.sessionId = QStringLiteral("default");
        profile.options.portName = QStringLiteral("COM7");
        profile.options.baudRate = 57600;
        profile.options.dataBits = QSerialPort::Data7;
        profile.options.parity = QSerialPort::EvenParity;
        profile.options.stopBits = QSerialPort::TwoStop;
        profile.options.flowControl = QSerialPort::HardwareControl;
        profile.options.dataTerminalReady = true;
        profile.options.requestToSend = true;

        QVERIFY2(store.saveSerialProfile(profile), qPrintable(store.lastErrorText()));
        const auto loaded = store.latestSerialProfile();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->options.portName, QStringLiteral("COM7"));
        QCOMPARE(loaded->options.baudRate, 57600);
        QCOMPARE(loaded->options.dataBits, QSerialPort::Data7);
        QCOMPARE(loaded->options.parity, QSerialPort::EvenParity);
        QCOMPARE(loaded->options.stopBits, QSerialPort::TwoStop);
        QCOMPARE(loaded->options.flowControl, QSerialPort::HardwareControl);
        QVERIFY(loaded->options.dataTerminalReady);
        QVERIFY(loaded->options.requestToSend);
    }

    void updatingSameNameOverwritesProfile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("profiles.sqlite"))), qPrintable(store.lastErrorText()));

        svm::storage::SerialProfile first;
        first.name = QStringLiteral("default");
        first.options.portName = QStringLiteral("COM1");
        first.options.baudRate = 9600;
        first.options.dataBits = QSerialPort::Data8;
        first.options.parity = QSerialPort::NoParity;
        first.options.stopBits = QSerialPort::OneStop;
        first.options.flowControl = QSerialPort::NoFlowControl;
        first.options.dataTerminalReady = false;
        first.options.requestToSend = false;
        QVERIFY(store.saveSerialProfile(first));

        svm::storage::SerialProfile second;
        second.name = QStringLiteral("default");
        second.options.portName = QStringLiteral("COM2");
        second.options.baudRate = 115200;
        second.options.dataBits = QSerialPort::Data6;
        second.options.parity = QSerialPort::MarkParity;
        second.options.stopBits = QSerialPort::OneAndHalfStop;
        second.options.flowControl = QSerialPort::SoftwareControl;
        second.options.dataTerminalReady = true;
        second.options.requestToSend = true;
        QVERIFY(store.saveSerialProfile(second));

        const auto loaded = store.latestSerialProfile();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->options.portName, QStringLiteral("COM2"));
        QCOMPARE(loaded->options.baudRate, 115200);
        QCOMPARE(loaded->options.dataBits, QSerialPort::Data6);
        QCOMPARE(loaded->options.parity, QSerialPort::MarkParity);
        QCOMPARE(loaded->options.stopBits, QSerialPort::OneAndHalfStop);
        QCOMPARE(loaded->options.flowControl, QSerialPort::SoftwareControl);
        QVERIFY(loaded->options.dataTerminalReady);
        QVERIFY(loaded->options.requestToSend);
    }
};

QTEST_MAIN(SerialProfileTests)
#include "serial_profile_tests.moc"
