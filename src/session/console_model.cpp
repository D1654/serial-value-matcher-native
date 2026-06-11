#include "session/console_model.h"

#include <QMetaType>

namespace svm::session {

ConsoleModel::ConsoleModel(QObject* parent) : QObject(parent) {
    qRegisterMetaType<svm::session::ConsoleLine>("svm::session::ConsoleLine");
}

void ConsoleModel::appendEvent(const capture::RawIoEvent& event) {
    ConsoleLine line = makeLine(event);
    m_lines.append(line);
    emit lineAdded(line);
}

void ConsoleModel::clear() {
    m_lines.clear();
    emit cleared();
}

const QVector<ConsoleLine>& ConsoleModel::lines() const {
    return m_lines;
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
    line.directionText = directionText(event.direction);
    line.timestampText = event.timestampUtc.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    line.hexText = formatHex(event.payload);
    line.textPreview = formatTextPreview(event.payload);
    line.displayLine = QStringLiteral("[%1] %2 %3 HEX=%4 TEXT=%5")
        .arg(line.timestampText, line.directionText, event.endpoint, line.hexText, line.textPreview);
    return line;
}

} // namespace svm::session
