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

    void appendRawEvents_persistsBatchCount() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("batch.sqlite"))), qPrintable(store.lastErrorText()));

        QList<svm::capture::RawIoEvent> events;
        for (int index = 0; index < 3; ++index) {
            svm::capture::RawIoEvent event;
            event.sessionId = QStringLiteral("batch-session");
            event.direction = index % 2 == 0 ? svm::capture::Direction::Rx : svm::capture::Direction::Tx;
            event.timestampUtc = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:00.000Z"), Qt::ISODateWithMs).addMSecs(index);
            event.endpoint = QStringLiteral("COM1");
            event.payload = QByteArray(1, static_cast<char>(index));
            events.append(event);
        }

        QVERIFY2(store.appendRawEvents(events), qPrintable(store.lastErrorText()));
        QCOMPARE(store.rawEventCount(), 3);
    }

    void exposesNarrowSessionStorePort() {
        using Store = svm::storage::SessionStore;
        using Traits = svm::storage::SessionStorePortTraits<Store>;
        static_assert(svm::storage::SessionStorePort<Store>);
        static_assert(svm::storage::SessionStoreReadDiagnosticsPort<Store>);

        const auto descriptor = Traits::descriptor;
        QVERIFY(descriptor.backendKind == svm::storage::SessionStoreBackendKind::QtSql);
        QVERIFY(descriptor.supportsRawIo);
        QVERIFY(descriptor.supportsModbusScans);
        QVERIFY(!descriptor.supportsUiPreferences);
        QVERIFY(!descriptor.exposesBackendFiles);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        Store store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("port.sqlite"))), qPrintable(store.lastErrorText()));

        Traits::RawIoEvent event;
        event.sessionId = QStringLiteral("port-session");
        event.direction = svm::capture::Direction::Rx;
        event.timestampUtc = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:00.000Z"), Qt::ISODateWithMs);
        event.endpoint = QStringLiteral("COM9");
        event.payload = QByteArray::fromHex("0103021234");

        QVERIFY2(store.appendRawEvent(event), qPrintable(store.lastErrorText()));
        QCOMPARE(store.rawEventCount(), 1);

        Traits::ScanExecution execution;
        execution.session.sessionId = QStringLiteral("port-scan");
        execution.session.slaveId = 1;
        execution.session.functionCode = 3;
        execution.session.startAddress = 400;
        execution.session.endAddress = 401;
        execution.session.blockSize = 2;
        execution.session.requestCount = 1;
        execution.session.status = QStringLiteral("completed");
        execution.session.startedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:01.000Z"), Qt::ISODateWithMs);
        execution.session.finishedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:02.000Z"), Qt::ISODateWithMs);
        execution.session.successBlockCount = 1;

        svm::storage::ScanAttemptRecord attempt;
        attempt.blockIndex = 0;
        attempt.attemptIndex = 0;
        attempt.startAddress = 400;
        attempt.quantity = 2;
        attempt.status = QStringLiteral("success");
        attempt.requestFrame = QByteArray::fromHex("010301900002");
        attempt.responseFrame = QByteArray::fromHex("01030400010002");
        attempt.sentAtUtc = execution.session.startedAtUtc;
        attempt.receivedAtUtc = execution.session.finishedAtUtc;
        attempt.endpoint = QStringLiteral("COM9");
        execution.attempts.append(attempt);

        for (int offset = 0; offset < 2; ++offset) {
            svm::storage::ScanObservationRecord observation;
            observation.blockIndex = 0;
            observation.attemptIndex = 0;
            observation.slaveId = 1;
            observation.functionCode = 3;
            observation.address = 400 + offset;
            observation.value = 100 + offset;
            observation.observedAtUtc = execution.session.finishedAtUtc;
            execution.observations.append(observation);
        }

        QVERIFY2(store.saveScanExecution(execution), qPrintable(store.lastErrorText()));
        const auto loaded = store.scanSession(QStringLiteral("port-scan"));
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->startAddress, 400);
        QCOMPARE(static_cast<int>(store.scanAttempts(QStringLiteral("port-scan")).size()), 1);
        QCOMPARE(static_cast<int>(store.scanObservations(QStringLiteral("port-scan")).size()), 2);
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
