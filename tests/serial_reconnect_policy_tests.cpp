#include <QObject>
#include <QTest>

#include "transport/serial_reconnect_policy.h"

class SerialReconnectPolicyTests final : public QObject {
    Q_OBJECT

private slots:
    void ignoresRecoverableErrorWhenDisabled() {
        svm::transport::SerialReconnectPolicy policy;
        policy.recordSuccessfulOpen(optionsFor(QStringLiteral("COM3")));

        QVERIFY(!policy.enterWaitingOnError(QSerialPort::ResourceError));
        QVERIFY(!policy.isWaiting());
    }

    void entersWaitingForRecoverableErrorWhenEnabledAndLastOpenExists() {
        svm::transport::SerialReconnectPolicy policy;
        policy.setEnabled(true);
        policy.recordSuccessfulOpen(optionsFor(QStringLiteral("COM3")));

        QVERIFY(policy.enterWaitingOnError(QSerialPort::ResourceError));
        QVERIFY(policy.isWaiting());
        QCOMPARE(policy.waitingPortName(), QStringLiteral("COM3"));
    }

    void doesNotEnterWaitingForNonRecoverableError() {
        svm::transport::SerialReconnectPolicy policy;
        policy.setEnabled(true);
        policy.recordSuccessfulOpen(optionsFor(QStringLiteral("COM3")));

        QVERIFY(!policy.enterWaitingOnError(QSerialPort::PermissionError));
        QVERIFY(!policy.isWaiting());
    }

    void attemptsReconnectOnlyAfterWaitingPortAppears() {
        svm::transport::SerialReconnectPolicy policy;
        policy.setEnabled(true);
        policy.recordSuccessfulOpen(optionsFor(QStringLiteral("COM7")));
        QVERIFY(policy.enterWaitingOnError(QSerialPort::DeviceNotFoundError));

        QVERIFY(!policy.shouldAttemptReconnect({QStringLiteral("COM1"), QStringLiteral("COM3")}));
        QVERIFY(policy.shouldAttemptReconnect({QStringLiteral("COM1"), QStringLiteral("COM7")}));

        const auto reconnectOptions = policy.markAttemptIfReady({QStringLiteral("COM7")});
        QVERIFY(reconnectOptions.has_value());
        QCOMPARE(reconnectOptions->portName, QStringLiteral("COM7"));
        QVERIFY(!policy.shouldAttemptReconnect({QStringLiteral("COM7")}));
    }

    void disablingPolicyClearsWaitingState() {
        svm::transport::SerialReconnectPolicy policy;
        policy.setEnabled(true);
        policy.recordSuccessfulOpen(optionsFor(QStringLiteral("COM3")));
        QVERIFY(policy.enterWaitingOnError(QSerialPort::ResourceError));

        policy.setEnabled(false);
        QVERIFY(!policy.isWaiting());
        QVERIFY(!policy.shouldAttemptReconnect({QStringLiteral("COM3")}));
    }

private:
    static svm::transport::SerialOpenOptions optionsFor(const QString& portName) {
        svm::transport::SerialOpenOptions options;
        options.sessionId = QStringLiteral("test-session");
        options.portName = portName;
        options.baudRate = QSerialPort::Baud57600;
        return options;
    }
};

QTEST_MAIN(SerialReconnectPolicyTests)
#include "serial_reconnect_policy_tests.moc"
