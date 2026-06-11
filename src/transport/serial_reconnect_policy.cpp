#include "transport/serial_reconnect_policy.h"

namespace svm::transport {
namespace {

bool containsPort(const QStringList& availablePortNames, const QString& portName) {
    const QString desired = portName.trimmed();
    if (desired.isEmpty()) {
        return false;
    }

    for (const QString& availablePortName : availablePortNames) {
        if (availablePortName.trimmed().compare(desired, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}

} // namespace

void SerialReconnectPolicy::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!m_enabled) {
        clearWaiting();
    }
}

bool SerialReconnectPolicy::enabled() const {
    return m_enabled;
}

void SerialReconnectPolicy::recordSuccessfulOpen(const SerialOpenOptions& options) {
    m_lastSuccessfulOpen = options;
    clearWaiting();
}

bool SerialReconnectPolicy::hasLastSuccessfulOpen() const {
    return m_lastSuccessfulOpen.has_value();
}

std::optional<SerialOpenOptions> SerialReconnectPolicy::lastSuccessfulOpen() const {
    return m_lastSuccessfulOpen;
}

bool SerialReconnectPolicy::enterWaitingOnError(QSerialPort::SerialPortError error) {
    if (!m_enabled || !m_lastSuccessfulOpen.has_value() || !isRecoverableError(error)) {
        return false;
    }

    m_waitingOptions = m_lastSuccessfulOpen;
    m_attemptedForCurrentWait = false;
    return true;
}

bool SerialReconnectPolicy::isWaiting() const {
    return m_waitingOptions.has_value();
}

QString SerialReconnectPolicy::waitingPortName() const {
    return m_waitingOptions.has_value() ? m_waitingOptions->portName : QString();
}

void SerialReconnectPolicy::clearWaiting() {
    m_waitingOptions.reset();
    m_attemptedForCurrentWait = false;
}

bool SerialReconnectPolicy::shouldAttemptReconnect(const QStringList& availablePortNames) const {
    return m_enabled
        && m_waitingOptions.has_value()
        && !m_attemptedForCurrentWait
        && containsPort(availablePortNames, m_waitingOptions->portName);
}

std::optional<SerialOpenOptions> SerialReconnectPolicy::markAttemptIfReady(const QStringList& availablePortNames) {
    if (!shouldAttemptReconnect(availablePortNames)) {
        return std::nullopt;
    }

    m_attemptedForCurrentWait = true;
    return m_waitingOptions;
}

bool SerialReconnectPolicy::isRecoverableError(QSerialPort::SerialPortError error) {
    return error == QSerialPort::ResourceError || error == QSerialPort::DeviceNotFoundError;
}

} // namespace svm::transport
