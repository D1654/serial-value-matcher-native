#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "capture/raw_io_event.h"

namespace svm::session {

struct ConsoleLine {
    capture::RawIoEvent event;
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
    void clear();
    const QVector<ConsoleLine>& lines() const;

    static QString directionText(capture::Direction direction);
    static QString formatHex(const QByteArray& payload);
    static QString formatTextPreview(const QByteArray& payload);
    static ConsoleLine makeLine(const capture::RawIoEvent& event);

signals:
    void lineAdded(const svm::session::ConsoleLine& line);
    void cleared();

private:
    QVector<ConsoleLine> m_lines;
};

} // namespace svm::session

Q_DECLARE_METATYPE(svm::session::ConsoleLine)
