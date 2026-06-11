#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "storage/session_store.h"

namespace {

QStringList sortedConnectionNames()
{
    QStringList names = QSqlDatabase::connectionNames();
    names.sort();
    return names;
}

bool execSql(QSqlDatabase& db, const QString& sql, QString* errorText)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        if (errorText != nullptr) {
            *errorText = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool createOldMigrationSchema(const QString& databasePath, QString* errorText)
{
    const QString connectionName = QStringLiteral("old-schema-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        ok = db.open();
        if (!ok && errorText != nullptr) {
            *errorText = db.lastError().text();
        }
        if (ok) {
            ok = execSql(db, QStringLiteral(R"sql(
                CREATE TABLE protocol_field_rules (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    rule_id TEXT NOT NULL UNIQUE,
                    field_name TEXT NOT NULL,
                    source_stability_run_id TEXT NOT NULL,
                    source_stable_candidate_id INTEGER NOT NULL,
                    candidate_type TEXT NOT NULL,
                    word_order TEXT NOT NULL,
                    byte_order TEXT NOT NULL,
                    slave_id INTEGER NOT NULL,
                    function_code INTEGER NOT NULL,
                    start_address INTEGER NOT NULL,
                    register_count INTEGER NOT NULL,
                    scale_multiplier REAL NOT NULL,
                    scale_offset REAL NOT NULL,
                    unit TEXT NOT NULL,
                    confidence_level TEXT NOT NULL,
                    stability_score REAL NOT NULL,
                    evidence_summary TEXT NOT NULL,
                    created_at_utc TEXT NOT NULL
                )
            )sql"), errorText);
        }
        if (ok) {
            ok = execSql(db, QStringLiteral(R"sql(
                CREATE TABLE rule_verification_results (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    verification_run_id TEXT NOT NULL,
                    rule_id TEXT NOT NULL,
                    field_name TEXT NOT NULL,
                    unit TEXT NOT NULL,
                    source_scan_session_id TEXT NOT NULL,
                    verified INTEGER NOT NULL,
                    status_text TEXT NOT NULL,
                    slave_id INTEGER NOT NULL,
                    function_code INTEGER NOT NULL,
                    start_address INTEGER NOT NULL,
                    register_count INTEGER NOT NULL,
                    observation_ids TEXT NOT NULL,
                    raw_registers TEXT NOT NULL,
                    decoded_value REAL NOT NULL,
                    engineering_value REAL NOT NULL,
                    observed_at_utc TEXT NOT NULL,
                    evidence_text TEXT NOT NULL
                )
            )sql"), errorText);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool tableHasColumn(const QString& databasePath, const QString& tableName, const QString& columnName, QString* errorText)
{
    const QString connectionName = QStringLiteral("column-check-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool found = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        if (!db.open()) {
            if (errorText != nullptr) {
                *errorText = db.lastError().text();
            }
        } else {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("PRAGMA table_info(\"%1\")").arg(tableName))) {
                if (errorText != nullptr) {
                    *errorText = query.lastError().text();
                }
            } else {
                while (query.next()) {
                    if (query.value(1).toString() == columnName) {
                        found = true;
                        break;
                    }
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return found;
}

} // namespace

class SessionStoreTests final : public QObject {
    Q_OBJECT

private slots:
    void appendRawEvent_persistsCount() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("test.sqlite"))), qPrintable(store.lastErrorText()));
        QCOMPARE(store.rawEventCount(), 0);

        svm::capture::RawIoEvent event;
        event.sessionId = QStringLiteral("test-session");
        event.direction = svm::capture::Direction::Rx;
        event.timestampUtc = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:00.000Z"), Qt::ISODateWithMs);
        event.endpoint = QStringLiteral("COM1");
        event.payload = QByteArray::fromHex("010203");

        QVERIFY2(store.appendRawEvent(event), qPrintable(store.lastErrorText()));
        QCOMPARE(store.rawEventCount(), 1);
    }

    void removesSqlConnectionWhenDestroyed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QStringList before = sortedConnectionNames();

        {
            svm::storage::SessionStore store;
            QVERIFY2(store.open(dir.filePath(QStringLiteral("connection-lifecycle.sqlite"))), qPrintable(store.lastErrorText()));
            QStringList during = sortedConnectionNames();
            QVERIFY(during.size() == before.size() + 1);
        }

        QCOMPARE(sortedConnectionNames(), before);
    }

    void reopeningStoreKeepsSingleOwnedConnection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QStringList before = sortedConnectionNames();

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("first.sqlite"))), qPrintable(store.lastErrorText()));
        QCOMPARE(sortedConnectionNames().size(), before.size() + 1);

        QVERIFY2(store.open(dir.filePath(QStringLiteral("second.sqlite"))), qPrintable(store.lastErrorText()));
        QCOMPARE(sortedConnectionNames().size(), before.size() + 1);
    }

    void upgradesOldMigrationSchemaWithWhitelistedColumns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString databasePath = dir.filePath(QStringLiteral("old-migration-schema.sqlite"));
        QString errorText;
        QVERIFY2(createOldMigrationSchema(databasePath, &errorText), qPrintable(errorText));

        {
            svm::storage::SessionStore store;
            QVERIFY2(store.open(databasePath), qPrintable(store.lastErrorText()));
        }

        QVERIFY2(tableHasColumn(databasePath, QStringLiteral("protocol_field_rules"), QStringLiteral("interpretation_map"), &errorText), qPrintable(errorText));
        QVERIFY2(tableHasColumn(databasePath, QStringLiteral("rule_verification_results"), QStringLiteral("interpretation_text"), &errorText), qPrintable(errorText));
        QVERIFY2(tableHasColumn(databasePath, QStringLiteral("rule_verification_results"), QStringLiteral("candidate_type"), &errorText), qPrintable(errorText));
    }
};

QTEST_MAIN(SessionStoreTests)
#include "session_store_tests.moc"
