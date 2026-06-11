#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include "capture/direction.h"

namespace svm::capture {

struct RawIoEvent {
    QString sessionId;
    Direction direction = Direction::Rx;
    QDateTime timestampUtc = QDateTime::currentDateTimeUtc();
    QString endpoint;
    QByteArray payload;
};

} // namespace svm::capture

Q_DECLARE_METATYPE(svm::capture::RawIoEvent)
