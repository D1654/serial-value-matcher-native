#include <QObject>
#include <QTest>

#include "transport/serial_port_selection.h"

class SerialPortSelectionTests final : public QObject {
    Q_OBJECT

private slots:
    void keepsCurrentPortWhenItStillExists() {
        const auto selection = svm::transport::SerialPortSelectionPolicy::choose(
            QStringLiteral("COM3"),
            QStringLiteral("COM7"),
            {QStringLiteral("COM1"), QStringLiteral("COM3"), QStringLiteral("COM7")});

        QVERIFY(selection.hasSelection());
        QCOMPARE(selection.portName, QStringLiteral("COM3"));
        QCOMPARE(selection.reason, svm::transport::SerialPortSelectionReason::KeepCurrent);
    }

    void restoresProfilePortWhenCurrentPortDisappears() {
        const auto selection = svm::transport::SerialPortSelectionPolicy::choose(
            QStringLiteral("COM3"),
            QStringLiteral("COM7"),
            {QStringLiteral("COM1"), QStringLiteral("COM7")});

        QVERIFY(selection.hasSelection());
        QCOMPARE(selection.portName, QStringLiteral("COM7"));
        QCOMPARE(selection.reason, svm::transport::SerialPortSelectionReason::RestoreProfile);
    }

    void selectsFirstAvailablePortWhenNeitherCurrentNorProfileExists() {
        const auto selection = svm::transport::SerialPortSelectionPolicy::choose(
            QStringLiteral("COM3"),
            QStringLiteral("COM7"),
            {QStringLiteral("COM11"), QStringLiteral("COM12")});

        QVERIFY(selection.hasSelection());
        QCOMPARE(selection.portName, QStringLiteral("COM11"));
        QCOMPARE(selection.reason, svm::transport::SerialPortSelectionReason::FirstAvailable);
    }

    void matchesPortsCaseInsensitivelyAndKeepsEnumeratedName() {
        const auto selection = svm::transport::SerialPortSelectionPolicy::choose(
            QStringLiteral("com3"),
            QString(),
            {QStringLiteral("COM3")});

        QVERIFY(selection.hasSelection());
        QCOMPARE(selection.portName, QStringLiteral("COM3"));
        QCOMPARE(selection.reason, svm::transport::SerialPortSelectionReason::KeepCurrent);
    }

    void returnsNoSelectionWhenNoPortsAreAvailable() {
        const auto selection = svm::transport::SerialPortSelectionPolicy::choose(
            QStringLiteral("COM3"),
            QStringLiteral("COM7"),
            {});

        QVERIFY(!selection.hasSelection());
        QCOMPARE(selection.reason, svm::transport::SerialPortSelectionReason::NoneAvailable);
    }
};

QTEST_MAIN(SerialPortSelectionTests)
#include "serial_port_selection_tests.moc"
