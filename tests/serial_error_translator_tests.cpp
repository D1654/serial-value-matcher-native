#include <QtTest/QtTest>

#include "transport/serial_error_translator.h"

class SerialErrorTranslatorTests final : public QObject {
    Q_OBJECT

private slots:
    void permissionErrorExplainsBusyOrPermission() {
        const QString message = svm::transport::SerialErrorTranslator::translate(
            QSerialPort::PermissionError,
            QStringLiteral("Permission denied"));
        QVERIFY(message.contains(QStringLiteral("占用")) || message.contains(QStringLiteral("权限")));
        QVERIFY(message.contains(QStringLiteral("Permission denied")));
    }

    void deviceNotFoundExplainsMissingOrUnplugged() {
        const QString message = svm::transport::SerialErrorTranslator::translate(QSerialPort::DeviceNotFoundError);
        QVERIFY(message.contains(QStringLiteral("没有找到")));
        QVERIFY(message.contains(QStringLiteral("拔出")) || message.contains(QStringLiteral("刷新")));
    }

    void resourceErrorExplainsDisconnect() {
        const QString message = svm::transport::SerialErrorTranslator::translate(QSerialPort::ResourceError);
        QVERIFY(message.contains(QStringLiteral("拔出")) || message.contains(QStringLiteral("异常")));
    }

    void timeoutErrorExplainsNoResponse() {
        const QString message = svm::transport::SerialErrorTranslator::translate(QSerialPort::TimeoutError);
        QVERIFY(message.contains(QStringLiteral("超时")));
        QVERIFY(message.contains(QStringLiteral("没有响应")) || message.contains(QStringLiteral("不匹配")));
    }


    void controlLineFailureMentionsSignalAndOriginalError() {
        const QString message = svm::transport::SerialErrorTranslator::controlLineFailureMessage(
            QStringLiteral("DTR"),
            QStringLiteral("Operation not supported"));
        QVERIFY(message.contains(QStringLiteral("DTR")));
        QVERIFY(message.contains(QStringLiteral("失败")));
        QVERIFY(message.contains(QStringLiteral("不支持")) || message.contains(QStringLiteral("控制线")));
        QVERIFY(message.contains(QStringLiteral("Operation not supported")));
    }

    void writeResultAcceptsCompleteOrEmptyPayload() {
        QVERIFY(svm::transport::SerialErrorTranslator::writeResultMessage(0, 0).isEmpty());
        QVERIFY(svm::transport::SerialErrorTranslator::writeResultMessage(3, 3).isEmpty());
    }

    void writeResultExplainsDriverWriteFailure() {
        const QString message = svm::transport::SerialErrorTranslator::writeResultMessage(
            3,
            -1,
            QStringLiteral("device removed"));
        QVERIFY(message.contains(QStringLiteral("发送失败")));
        QVERIFY(message.contains(QStringLiteral("驱动")) || message.contains(QStringLiteral("端口")));
        QVERIFY(message.contains(QStringLiteral("device removed")));
    }

    void writeResultExplainsZeroBytesWritten() {
        const QString message = svm::transport::SerialErrorTranslator::writeResultMessage(3, 0);
        QVERIFY(message.contains(QStringLiteral("没有写入任何字节")));
        QVERIFY(message.contains(QStringLiteral("流控")) || message.contains(QStringLiteral("端口")));
    }

    void writeResultExplainsPartialWrite() {
        const QString message = svm::transport::SerialErrorTranslator::writeResultMessage(8, 3);
        QVERIFY(message.contains(QStringLiteral("发送不完整")));
        QVERIFY(message.contains(QStringLiteral("8")));
        QVERIFY(message.contains(QStringLiteral("3")));
        QVERIFY(message.contains(QStringLiteral("流控")) || message.contains(QStringLiteral("缓冲区")));
    }

    void noErrorReturnsEmptyMessage() {
        QVERIFY(svm::transport::SerialErrorTranslator::translate(QSerialPort::NoError).isEmpty());
    }
};

QTEST_MAIN(SerialErrorTranslatorTests)
#include "serial_error_translator_tests.moc"
