#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "capture/raw_io_event.h"
#include "capture/session_evidence.h"

namespace svm::session {

struct ConsoleLine {
    capture::RawIoEvent event;
    quint64 evidenceOrder = 0;
    QString sessionId;
    QString sourceSubsystem;
    QString directionText;
    QString timestampText;
    QString hexText;
    QString textPreview;
    QString displayLine;
};

class ConsoleModel final : public QObject {
    Q_OBJECT

public:
    explicit ConsoleModel(QObject* parent = nullptr);

    void appendEvent(const capture::RawIoEvent& event);
    void appendEvidence(const capture::SessionEvidenceEvent& evidence);
    void clear();
    const QVector<ConsoleLine>& lines() const;
    void setMaximumLineCount(int maximumLineCount);
    int maximumLineCount() const;

    static QString directionText(capture::Direction direction);
    static QString formatHex(const QByteArray& payload);
    static QString formatTextPreview(const QByteArray& payload);
    static ConsoleLine makeLine(const capture::RawIoEvent& event);
    static ConsoleLine makeLine(const capture::SessionEvidenceEvent& evidence);

signals:
    void lineAdded(const svm::session::ConsoleLine& line);
    void cleared();

private:
    void trimToMaximumLineCount();

    QVector<ConsoleLine> m_lines;
    int m_maximumLineCount = 5000;
};

} // namespace svm::session

Q_DECLARE_METATYPE(svm::session::ConsoleLine)
