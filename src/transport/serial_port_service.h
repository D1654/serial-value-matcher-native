#pragma once

#include <cstddef>

#include <QObject>
#include <QSerialPort>
#include <QString>

#include "capture/capture_bus.h"
#include "transport/serial_write_queue.h"

namespace svm::transport {

struct SerialOpenOptions {
    QString sessionId;
    QString portName;
    qint32 baudRate = QSerialPort::Baud115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    bool dataTerminalReady = false;
    bool requestToSend = false;
    std::size_t writeQueueCapacity = kDefaultSerialWriteQueueCapacity;
};

class SerialPortService final : public QObject {
    Q_OBJECT

public:
    explicit SerialPortService(capture::CaptureBus& captureBus, QObject* parent = nullptr);

    bool open(const SerialOpenOptions& options);
    void close();
    bool isOpen() const;
    QString lastErrorText() const;
    std::size_t writeQueueCapacity() const noexcept;

public slots:
    qint64 writeBytes(const QByteArray& payload);

signals:
    void opened(QString portName);
    void closed();
    void errorOccurred(QString message);
    void serialErrorOccurred(QSerialPort::SerialPortError error, QString message);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    void reportError(QSerialPort::SerialPortError error, const QString& message);
    QString endpoint() const;

    capture::CaptureBus& m_captureBus;
    QSerialPort m_port;
    SerialOpenOptions m_options;
    QString m_lastErrorText;
};

} // namespace svm::transport
