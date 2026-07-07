#include "transport/serial_port_service.h"

#include "transport/serial_error_translator.h"

#include <QDateTime>

namespace svm::transport {

SerialPortService::SerialPortService(capture::CaptureBus& captureBus, QObject* parent)
    : QObject(parent), m_captureBus(captureBus) {
    connect(&m_port, &QSerialPort::readyRead, this, &SerialPortService::onReadyRead);
    connect(&m_port, &QSerialPort::errorOccurred, this, &SerialPortService::onErrorOccurred);
}

bool SerialPortService::open(const SerialOpenOptions& options) {
    if (m_port.isOpen()) {
        m_port.close();
    }

    m_options = options;
    m_port.setPortName(options.portName);
    m_port.setBaudRate(options.baudRate);
    m_port.setDataBits(options.dataBits);
    m_port.setParity(options.parity);
    m_port.setStopBits(options.stopBits);
    m_port.setFlowControl(options.flowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        reportError(m_port.error(), SerialErrorTranslator::translate(m_port.error(), m_port.errorString()));
        return false;
    }

    if (!m_port.setDataTerminalReady(options.dataTerminalReady)) {
        reportError(QSerialPort::UnsupportedOperationError,
            SerialErrorTranslator::controlLineFailureMessage(QStringLiteral("DTR"), m_port.errorString()));
        m_port.close();
        return false;
    }

    if (!m_port.setRequestToSend(options.requestToSend)) {
        reportError(QSerialPort::UnsupportedOperationError,
            SerialErrorTranslator::controlLineFailureMessage(QStringLiteral("RTS"), m_port.errorString()));
        m_port.close();
        return false;
    }

    m_lastErrorText.clear();
    emit opened(options.portName);
    return true;
}

void SerialPortService::close() {
    if (!m_port.isOpen()) {
        return;
    }

    m_port.close();
    emit closed();
}

bool SerialPortService::isOpen() const {
    return m_port.isOpen();
}

QString SerialPortService::lastErrorText() const {
    return m_lastErrorText;
}

std::size_t SerialPortService::writeQueueCapacity() const noexcept {
    return m_options.writeQueueCapacity;
}

qint64 SerialPortService::writeBytes(const QByteArray& payload) {
    if (!m_port.isOpen()) {
        reportError(QSerialPort::NotOpenError, QStringLiteral("串口未打开，无法发送数据。"));
        return -1;
    }

    if (payload.isEmpty()) {
        return 0;
    }

    const qint64 written = m_port.write(payload);
    if (written > 0) {
        capture::RawIoEvent event;
        event.sessionId = m_options.sessionId;
        event.direction = capture::Direction::Tx;
        event.timestampUtc = QDateTime::currentDateTimeUtc();
        event.endpoint = endpoint();
        event.payload = payload.left(static_cast<int>(written));
        m_captureBus.publish(event);
    }

    const QString writeError = SerialErrorTranslator::writeResultMessage(payload.size(), written, m_port.errorString());
    if (!writeError.isEmpty()) {
        reportError(QSerialPort::WriteError, writeError);
    } else {
        m_lastErrorText.clear();
    }

    return written;
}

void SerialPortService::onReadyRead() {
    const QByteArray payload = m_port.readAll();
    if (payload.isEmpty()) {
        return;
    }

    capture::RawIoEvent event;
    event.sessionId = m_options.sessionId;
    event.direction = capture::Direction::Rx;
    event.timestampUtc = QDateTime::currentDateTimeUtc();
    event.endpoint = endpoint();
    event.payload = payload;
    m_captureBus.publish(event);
}

void SerialPortService::onErrorOccurred(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError) {
        return;
    }

    reportError(error, SerialErrorTranslator::translate(error, m_port.errorString()));
}

void SerialPortService::reportError(QSerialPort::SerialPortError error, const QString& message) {
    m_lastErrorText = message;
    emit serialErrorOccurred(error, m_lastErrorText);
    emit errorOccurred(m_lastErrorText);
}

QString SerialPortService::endpoint() const {
    return m_options.portName;
}

} // namespace svm::transport
