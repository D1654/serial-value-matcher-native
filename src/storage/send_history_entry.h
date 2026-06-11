#pragma once

#include <QDateTime>
#include <QString>

#include "protocol/payload_codec.h"

namespace svm::storage {

struct SendHistoryEntry {
    qint64 id = 0;
    QString content;
    protocol::PayloadMode payloadMode = protocol::PayloadMode::Text;
    protocol::LineEnding lineEnding = protocol::LineEnding::None;
    QDateTime sentAtUtc;
};

} // namespace svm::storage
