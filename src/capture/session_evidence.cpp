#include "capture/session_evidence.h"

#include <utility>

namespace svm::capture {
namespace {

QString normalizedKey(QStringView key) {
    return key.toString().trimmed().toLower();
}

QDateTime normalizedTimestampUtc(const QDateTime& timestampUtc) {
    return timestampUtc.isValid() ? timestampUtc.toUTC() : QDateTime::currentDateTimeUtc();
}

QString normalizedSource(QString sourceSubsystem) {
    return sourceSubsystem.trimmed().isEmpty() ? QStringLiteral("capture_bus") : std::move(sourceSubsystem);
}

} // namespace

QString sessionEvidenceEventTypeKey(SessionEvidenceEventType type) {
    switch (type) {
    case SessionEvidenceEventType::RawTx:
        return QStringLiteral("raw_tx");
    case SessionEvidenceEventType::RawRx:
        return QStringLiteral("raw_rx");
    case SessionEvidenceEventType::UserCommand:
        return QStringLiteral("user_command");
    case SessionEvidenceEventType::ModbusScanSettings:
        return QStringLiteral("modbus_scan_settings");
    case SessionEvidenceEventType::MatchResult:
        return QStringLiteral("match_result");
    case SessionEvidenceEventType::ReportMetadata:
        return QStringLiteral("report_metadata");
    case SessionEvidenceEventType::AppVersion:
        return QStringLiteral("app_version");
    }
    return QStringLiteral("unknown");
}

std::optional<SessionEvidenceEventType> parseSessionEvidenceEventType(QStringView key) {
    const QString value = normalizedKey(key);
    if (value == QStringLiteral("raw_tx")) {
        return SessionEvidenceEventType::RawTx;
    }
    if (value == QStringLiteral("raw_rx")) {
        return SessionEvidenceEventType::RawRx;
    }
    if (value == QStringLiteral("user_command")) {
        return SessionEvidenceEventType::UserCommand;
    }
    if (value == QStringLiteral("modbus_scan_settings")) {
        return SessionEvidenceEventType::ModbusScanSettings;
    }
    if (value == QStringLiteral("match_result")) {
        return SessionEvidenceEventType::MatchResult;
    }
    if (value == QStringLiteral("report_metadata")) {
        return SessionEvidenceEventType::ReportMetadata;
    }
    if (value == QStringLiteral("app_version")) {
        return SessionEvidenceEventType::AppVersion;
    }
    return std::nullopt;
}

bool isRawIoEvidenceType(SessionEvidenceEventType type) noexcept {
    return type == SessionEvidenceEventType::RawTx || type == SessionEvidenceEventType::RawRx;
}

std::optional<Direction> rawIoDirection(SessionEvidenceEventType type) noexcept {
    switch (type) {
    case SessionEvidenceEventType::RawTx:
        return Direction::Tx;
    case SessionEvidenceEventType::RawRx:
        return Direction::Rx;
    case SessionEvidenceEventType::UserCommand:
    case SessionEvidenceEventType::ModbusScanSettings:
    case SessionEvidenceEventType::MatchResult:
    case SessionEvidenceEventType::ReportMetadata:
    case SessionEvidenceEventType::AppVersion:
        return std::nullopt;
    }
    return std::nullopt;
}

void SessionEvidenceEvent::setMetadata(QString key, QString value, EvidenceFieldPrivacy privacy) {
    key = key.trimmed();
    if (key.isEmpty()) {
        return;
    }

    metadata.insert(key, std::move(value));
    const bool alreadySensitive = sensitiveMetadataKeys.contains(key);
    if (privacy == EvidenceFieldPrivacy::Sensitive && !alreadySensitive) {
        sensitiveMetadataKeys.append(key);
    } else if (privacy == EvidenceFieldPrivacy::Public && alreadySensitive) {
        sensitiveMetadataKeys.removeAll(key);
    }
}

bool SessionEvidenceEvent::isMetadataSensitive(QStringView key) const {
    return sensitiveMetadataKeys.contains(key.toString().trimmed());
}

bool SessionEvidenceEvent::isRawIo() const noexcept {
    return isRawIoEvidenceType(type);
}

SessionEvidenceSequencer::SessionEvidenceSequencer(QString sessionId)
    : sessionId_(std::move(sessionId)) {}

quint64 SessionEvidenceSequencer::nextOrder() const noexcept {
    return nextOrder_;
}

void SessionEvidenceSequencer::reset(QString sessionId, quint64 firstOrder) noexcept {
    sessionId_ = std::move(sessionId);
    nextOrder_ = firstOrder == 0 ? 1 : firstOrder;
}

SessionEvidenceEvent SessionEvidenceSequencer::nextRawIoEvent(const RawIoEvent& event, QString sourceSubsystem) {
    SessionEvidenceEvent evidence = sessionEvidenceFromRawIoEvent(event, nextOrder_++, std::move(sourceSubsystem));
    if (evidence.sessionId.isEmpty()) {
        evidence.sessionId = sessionId_;
    }
    return evidence;
}

SessionEvidenceEvent SessionEvidenceSequencer::nextEvent(
    SessionEvidenceEventType type,
    QString sourceSubsystem,
    QDateTime timestampUtc) {
    SessionEvidenceEvent evidence;
    evidence.sessionId = sessionId_;
    evidence.order = nextOrder_++;
    evidence.timestampUtc = normalizedTimestampUtc(timestampUtc);
    evidence.type = type;
    evidence.sourceSubsystem = normalizedSource(std::move(sourceSubsystem));
    return evidence;
}

SessionEvidenceEvent sessionEvidenceFromRawIoEvent(
    const RawIoEvent& event,
    quint64 order,
    QString sourceSubsystem) {
    SessionEvidenceEvent evidence;
    evidence.sessionId = event.sessionId;
    evidence.order = order == 0 ? 1 : order;
    evidence.timestampUtc = normalizedTimestampUtc(event.timestampUtc);
    evidence.type = event.direction == Direction::Tx
        ? SessionEvidenceEventType::RawTx
        : SessionEvidenceEventType::RawRx;
    evidence.sourceSubsystem = normalizedSource(
        sourceSubsystem.trimmed().isEmpty() ? event.sourceSubsystem : std::move(sourceSubsystem));
    evidence.endpoint = event.endpoint;
    evidence.rawPayload = event.payload;
    return evidence;
}

} // namespace svm::capture
