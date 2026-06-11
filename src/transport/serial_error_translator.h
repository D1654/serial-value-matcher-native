#pragma once

#include <QSerialPort>
#include <QString>

namespace svm::transport {

class SerialErrorTranslator final {
public:
    static QString translate(QSerialPort::SerialPortError error, const QString& originalErrorText = {});
    static QString controlLineFailureMessage(const QString& signalName, const QString& originalErrorText = {});
    static QString writeResultMessage(qint64 expectedBytes, qint64 writtenBytes, const QString& originalErrorText = {});

private:
    static QString withOriginal(QString message, const QString& originalErrorText);
};

} // namespace svm::transport
