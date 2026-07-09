#pragma once

#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QtGlobal>

#include "capture/direction.h"
#include "capture/raw_io_event.h"

namespace svm::capture {

enum class SessionEvidenceEventType {
    RawTx,
    RawRx,
    UserCommand,
    ModbusScanSettings,
    CommandSequenceStep,
    CommandSequenceAssertion,
    MatchResult,
    ReportMetadata,
    AppVersion,
};

enum class EvidenceFieldPrivacy {
    Public,
    Sensitive,
};

struct SessionEvidenceEvent {
    QString sessionId;
    quint64 order = 0;
    QDateTime timestampUtc;
    SessionEvidenceEventType type = SessionEvidenceEventType::RawRx;
    QString sourceSubsystem;
    QString endpoint;
    QByteArray rawPayload;
    QMap<QString, QString> metadata;
    QStringList sensitiveMetadataKeys;

    void setMetadata(QString key, QString value, EvidenceFieldPrivacy privacy = EvidenceFieldPrivacy::Public);
    bool isMetadataSensitive(QStringView key) const;
    bool isRawIo() const noexcept;
};

class SessionEvidenceSequencer final {
public:
    explicit SessionEvidenceSequencer(QString sessionId = {});

    quint64 nextOrder() const noexcept;
    void reset(QString sessionId = {}, quint64 firstOrder = 1) noexcept;
    SessionEvidenceEvent nextRawIoEvent(const RawIoEvent& event, QString sourceSubsystem = {});
    SessionEvidenceEvent nextEvent(
        SessionEvidenceEventType type,
        QString sourceSubsystem,
        QDateTime timestampUtc = QDateTime::currentDateTimeUtc());

private:
    QString sessionId_;
    quint64 nextOrder_ = 1;
};

QString sessionEvidenceEventTypeKey(SessionEvidenceEventType type);
std::optional<SessionEvidenceEventType> parseSessionEvidenceEventType(QStringView key);
bool isRawIoEvidenceType(SessionEvidenceEventType type) noexcept;
std::optional<Direction> rawIoDirection(SessionEvidenceEventType type) noexcept;
SessionEvidenceEvent sessionEvidenceFromRawIoEvent(
    const RawIoEvent& event,
    quint64 order,
    QString sourceSubsystem = {});

} // namespace svm::capture

Q_DECLARE_METATYPE(svm::capture::SessionEvidenceEvent)
