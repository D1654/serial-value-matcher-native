#pragma once

#include <optional>

#include <QSerialPort>
#include <QString>
#include <QStringList>

#include "transport/serial_port_service.h"

namespace svm::transport {

class SerialReconnectPolicy final {
public:
    void setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const;

    void recordSuccessfulOpen(const SerialOpenOptions& options);
    [[nodiscard]] bool hasLastSuccessfulOpen() const;
    [[nodiscard]] std::optional<SerialOpenOptions> lastSuccessfulOpen() const;

    [[nodiscard]] bool enterWaitingOnError(QSerialPort::SerialPortError error);
    [[nodiscard]] bool isWaiting() const;
    [[nodiscard]] QString waitingPortName() const;
    void clearWaiting();

    [[nodiscard]] bool shouldAttemptReconnect(const QStringList& availablePortNames) const;
    [[nodiscard]] std::optional<SerialOpenOptions> markAttemptIfReady(const QStringList& availablePortNames);

    [[nodiscard]] static bool isRecoverableError(QSerialPort::SerialPortError error);

private:
    bool m_enabled = false;
    std::optional<SerialOpenOptions> m_lastSuccessfulOpen;
    std::optional<SerialOpenOptions> m_waitingOptions;
    bool m_attemptedForCurrentWait = false;
};

} // namespace svm::transport
