#pragma once

#include <QDateTime>
#include <QString>

#include "transport/serial_port_service.h"

namespace svm::storage {

struct SerialProfile {
    QString name = QStringLiteral("default");
    transport::SerialOpenOptions options;
    bool dataTerminalReady = false;
    bool requestToSend = false;
    QDateTime updatedAtUtc;
};

} // namespace svm::storage
