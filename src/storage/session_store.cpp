#include "storage/session_store.h"

#include <QDateTime>
#include <QStringList>
#include <QUuid>


namespace svm::storage {

QString SessionStore::dateToString(const QDateTime& value) {
    return value.isValid() ? value.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("");
}

QString SessionStore::notNullString(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

QDateTime SessionStore::dateFromString(const QString& value) {
    return QDateTime::fromString(value, Qt::ISODateWithMs);
}

QString SessionStore::joinLongList(const QList<qint64>& values) {
    QStringList parts;
    parts.reserve(values.size());
    for (const qint64 value : values) {
        parts.append(QString::number(value));
    }
    return parts.join(QLatin1Char(','));
}

QString SessionStore::joinIntList(const QList<int>& values) {
    QStringList parts;
    parts.reserve(values.size());
    for (const int value : values) {
        parts.append(QString::number(value));
    }
    return parts.join(QLatin1Char(','));
}

QString SessionStore::joinUInt16List(const QList<quint16>& values) {
    QStringList parts;
    parts.reserve(values.size());
    for (const quint16 value : values) {
        parts.append(QString::number(value));
    }
    return parts.join(QLatin1Char(','));
}

QString SessionStore::joinStringList(const QList<QString>& values) {
    QStringList parts;
    parts.reserve(values.size());
    for (const QString& value : values) {
        parts.append(value);
    }
    return parts.join(QLatin1Char(','));
}

QList<QString> SessionStore::parseStringList(const QString& text) {
    QList<QString> values;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        values.append(part);
    }
    return values;
}

QList<qint64> SessionStore::parseLongList(const QString& text) {
    QList<qint64> values;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        values.append(part.toLongLong());
    }
    return values;
}

QList<int> SessionStore::parseIntList(const QString& text) {
    QList<int> values;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        values.append(part.toInt());
    }
    return values;
}

QString SessionStore::candidateTypeKey(svm::matching::NumericCandidateType type) {
    return svm::matching::numericCandidateTypeName(type);
}

QString SessionStore::wordOrderKey(svm::matching::WordOrder order) {
    switch (order) {
    case svm::matching::WordOrder::HighWordFirst:
        return QStringLiteral("HighWordFirst");
    case svm::matching::WordOrder::LowWordFirst:
        return QStringLiteral("LowWordFirst");
    }
    return QStringLiteral("Unknown");
}

QString SessionStore::byteOrderKey(svm::matching::ByteOrder order) {
    switch (order) {
    case svm::matching::ByteOrder::BigEndian:
        return QStringLiteral("BigEndian");
    case svm::matching::ByteOrder::LittleEndian:
        return QStringLiteral("LittleEndian");
    }
    return QStringLiteral("Unknown");
}

SessionStore::SessionStore(QObject* parent)
    : QObject(parent), m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
SessionStore::~SessionStore() {
    closeDatabaseConnection();
}

bool SessionStore::hasReadError() const {
    return !m_lastReadErrorText.isEmpty();
}

QString SessionStore::lastReadErrorText() const {
    return m_lastReadErrorText;
}

void SessionStore::clearReadError() const {
    m_lastReadErrorText.clear();
}

void SessionStore::setReadError(const QString& operation, const QSqlError& error) const {
    const QString detail = error.text().trimmed().isEmpty() ? QStringLiteral("未知数据库错误") : error.text().trimmed();
    m_lastReadErrorText = QStringLiteral("%1：%2").arg(operation, detail);
}

QString SessionStore::lastErrorText() const {
    return m_lastErrorText;
}

} // namespace svm::storage
