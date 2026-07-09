#include "session/console_model.h"

#include <QMetaType>

namespace svm::session {
namespace {

svm::capture::RawIoEvent rawEventFromEvidence(const capture::SessionEvidenceEvent& evidence) {
    svm::capture::RawIoEvent event;
    event.sessionId = evidence.sessionId;
    event.timestampUtc = evidence.timestampUtc;
    event.sourceSubsystem = evidence.sourceSubsystem;
    event.endpoint = evidence.endpoint;
    event.payload = evidence.rawPayload;
    if (const auto direction = capture::rawIoDirection(evidence.type)) {
        event.direction = *direction;
    }
    return event;
}

} // namespace

ConsoleModel::ConsoleModel(QObject* parent) : QObject(parent) {
    qRegisterMetaType<svm::session::ConsoleLine>("svm::session::ConsoleLine");
}

void ConsoleModel::appendEvent(const capture::RawIoEvent& event) {
    ConsoleLine line = makeLine(event);
    m_lines.append(line);
    trimToMaximumLineCount();
    emit lineAdded(line);
}

void ConsoleModel::appendEvidence(const capture::SessionEvidenceEvent& evidence) {
    ConsoleLine line = makeLine(evidence);
    m_lines.append(line);
    trimToMaximumLineCount();
    emit lineAdded(line);
}

void ConsoleModel::clear() {
    m_lines.clear();
    emit cleared();
}

const QVector<ConsoleLine>& ConsoleModel::lines() const {
    return m_lines;
}

void ConsoleModel::setMaximumLineCount(int maximumLineCount) {
    m_maximumLineCount = maximumLineCount <= 0 ? 1 : maximumLineCount;
    trimToMaximumLineCount();
}

int ConsoleModel::maximumLineCount() const {
    return m_maximumLineCount;
}

QString ConsoleModel::directionText(capture::Direction direction) {
    return direction == capture::Direction::Rx ? QStringLiteral("RX") : QStringLiteral("TX");
}

QString ConsoleModel::formatHex(const QByteArray& payload) {
    return QString::fromLatin1(payload.toHex(' ').toUpper());
}

QString ConsoleModel::formatTextPreview(const QByteArray& payload) {
    QString text = QString::fromUtf8(payload);
    text.replace(u'\r', QStringLiteral("\\r"));
    text.replace(u'\n', QStringLiteral("\\n"));
    return text;
}

ConsoleLine ConsoleModel::makeLine(const capture::RawIoEvent& event) {
    ConsoleLine line;
    line.event = event;
    line.sessionId = event.sessionId;
    line.sourceSubsystem = event.sourceSubsystem;
    line.directionText = directionText(event.direction);
    line.timestampText = event.timestampUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    line.hexText = formatHex(event.payload);
    line.textPreview = formatTextPreview(event.payload);
    line.displayLine = QStringLiteral("[%1] %2 %3 HEX=%4 TEXT=%5")
        .arg(line.timestampText, line.directionText, event.endpoint, line.hexText, line.textPreview);
    return line;
}

ConsoleLine ConsoleModel::makeLine(const capture::SessionEvidenceEvent& evidence) {
    if (evidence.isRawIo()) {
        ConsoleLine line = makeLine(rawEventFromEvidence(evidence));
        line.evidenceOrder = evidence.order;
        line.sessionId = evidence.sessionId;
        line.sourceSubsystem = evidence.sourceSubsystem;
        return line;
    }

    ConsoleLine line;
    line.evidenceOrder = evidence.order;
    line.sessionId = evidence.sessionId;
    line.sourceSubsystem = evidence.sourceSubsystem;
    line.directionText = capture::sessionEvidenceEventTypeKey(evidence.type).toUpper();
    line.timestampText = evidence.timestampUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    line.textPreview = evidence.metadata.value(QStringLiteral("summary"));
    line.displayLine = QStringLiteral("[%1] %2 %3")
        .arg(line.timestampText, line.directionText, line.textPreview);
    return line;
}

void ConsoleModel::trimToMaximumLineCount() {
    const int overflow = m_lines.size() - m_maximumLineCount;
    if (overflow > 0) {
        m_lines.remove(0, overflow);
    }
}

} // namespace svm::session
