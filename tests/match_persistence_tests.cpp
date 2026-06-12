#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "matching/value_candidate_generator.h"
#include "storage/session_store.h"

using namespace svm::matching;

namespace {

RegisterSample sample(int address, quint16 value)
{
    RegisterSample result;
    result.observationId = 9000 + address;
    result.sessionId = QStringLiteral("scan-persist-source");
    result.slaveId = 1;
    result.functionCode = 3;
    result.address = address;
    result.value = value;
    result.blockIndex = 4;
    result.attemptIndex = 0;
    result.observedAtUtc = QDateTime::currentDateTimeUtc();
    return result;
}

TargetValue target(double value)
{
    TargetValue result;
    result.label = QStringLiteral("温度");
    result.value = value;
    result.unit = QStringLiteral("℃");
    result.sampledAtUtc = QDateTime::currentDateTimeUtc();
    return result;
}

CandidateGenerationOptions floatOnlyOptions()
{
    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = true;
    options.tolerance.absolute = 0.001;
    return options;
}

svm::storage::MatchRunRecord matchRun(QString runId = QStringLiteral("match-run-1"))
{
    svm::storage::MatchRunRecord run;
    run.runId = runId;
    run.sourceScanSessionId = QStringLiteral("scan-persist-source");
    run.targetLabel = QStringLiteral("温度");
    run.targetValue = 12.34;
    run.targetUnit = QStringLiteral("℃");
    run.sampledAtUtc = QDateTime::currentDateTimeUtc();
    run.toleranceAbsolute = 0.001;
    run.toleranceRelativeRatio = 0.0;
    run.createdAtUtc = QDateTime::currentDateTimeUtc();
    return run;
}

QList<ValueMatchCandidate> generatedCandidates()
{
    QList<RegisterSample> samples;
    samples.append(sample(100, 0x4145));
    samples.append(sample(101, 0x70A4));

    const CandidateGenerationResult result = generateValueCandidates(samples, target(12.34), floatOnlyOptions());
    Q_ASSERT(result.success);
    Q_ASSERT(result.candidates.size() == 1);
    return result.candidates;
}

void dropTableForReadFailure(const QString& databasePath, const QString& tableName)
{
    const QString connectionName = QStringLiteral("drop-match-table-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        QVERIFY2(query.exec(QStringLiteral("DROP TABLE %1").arg(tableName)), qPrintable(query.lastError().text()));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

} // namespace

class MatchPersistenceTests final : public QObject {
    Q_OBJECT

private slots:
    void savesAndReloadsMatchRunWithCandidates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("matches.sqlite"))), qPrintable(store.lastErrorText()));

        const auto candidates = generatedCandidates();
        QVERIFY2(store.saveMatchRun(matchRun(), candidates), qPrintable(store.lastErrorText()));

        const auto run = store.matchRun(QStringLiteral("match-run-1"));
        QVERIFY(run.has_value());
        QCOMPARE(run->runId, QStringLiteral("match-run-1"));
        QCOMPARE(run->sourceScanSessionId, QStringLiteral("scan-persist-source"));
        QCOMPARE(run->targetLabel, QStringLiteral("温度"));
        QCOMPARE(run->targetValue, 12.34);
        QCOMPARE(run->targetUnit, QStringLiteral("℃"));
        QCOMPARE(run->toleranceAbsolute, 0.001);
        QCOMPARE(run->toleranceRelativeRatio, 0.0);
        QCOMPARE(run->candidateCount, 1);
        QVERIFY(run->createdAtUtc.isValid());

        const auto loaded = store.matchCandidates(QStringLiteral("match-run-1"));
        QCOMPARE(loaded.size(), 1);
        const svm::storage::MatchCandidateRecord& candidate = loaded.first();
        QCOMPARE(candidate.runId, QStringLiteral("match-run-1"));
        QCOMPARE(candidate.rankIndex, 0);
        QCOMPARE(candidate.candidateType, QStringLiteral("Float32"));
        QCOMPARE(candidate.wordOrder, QStringLiteral("HighWordFirst"));
        QCOMPARE(candidate.byteOrder, QStringLiteral("BigEndian"));
        QCOMPARE(candidate.sourceSessionId, QStringLiteral("scan-persist-source"));
        QCOMPARE(candidate.slaveId, 1);
        QCOMPARE(candidate.functionCode, 3);
        QCOMPARE(candidate.startAddress, 100);
        QCOMPARE(candidate.registerCount, 2);
        QCOMPARE(candidate.observationIds, QList<qint64>({9100, 9101}));
        QCOMPARE(candidate.addresses, QList<int>({100, 101}));
        QCOMPARE(candidate.blockIndexes, QList<int>({4, 4}));
        QCOMPARE(candidate.attemptIndexes, QList<int>({0, 0}));
        QCOMPARE(candidate.rawRegisters, QList<int>({0x4145, 0x70A4}));
        QVERIFY(qAbs(candidate.engineeringValue - 12.34) < 0.001);
        QVERIFY(candidate.absoluteError < 0.001);
        QVERIFY(candidate.score > 0.0);
        QVERIFY(candidate.observedAtUtc.isValid());
        QVERIFY(candidate.evidenceText.contains(QStringLiteral("单样本候选")));
    }

    void replacesCandidatesWhenSavingSameRunAgain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("replace.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY2(store.saveMatchRun(matchRun(QStringLiteral("replace-run")), generatedCandidates()), qPrintable(store.lastErrorText()));
        QCOMPARE(store.matchCandidates(QStringLiteral("replace-run")).size(), 1);

        svm::storage::MatchRunRecord rerun = matchRun(QStringLiteral("replace-run"));
        rerun.targetValue = 99.0;
        QVERIFY2(store.saveMatchRun(rerun, {}), qPrintable(store.lastErrorText()));

        const auto run = store.matchRun(QStringLiteral("replace-run"));
        QVERIFY(run.has_value());
        QCOMPARE(run->targetValue, 99.0);
        QCOMPARE(run->candidateCount, 0);
        QVERIFY(store.matchCandidates(QStringLiteral("replace-run")).isEmpty());
    }

    void reloadsRecentMatchRunsNewestFirst()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("recent-matches.sqlite"))), qPrintable(store.lastErrorText()));

        auto older = matchRun(QStringLiteral("older-run"));
        older.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-06-01T10:00:00.000Z"), Qt::ISODateWithMs);
        auto newer = matchRun(QStringLiteral("newer-run"));
        newer.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-06-01T10:05:00.000Z"), Qt::ISODateWithMs);

        QVERIFY2(store.saveMatchRun(older, generatedCandidates()), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveMatchRun(newer, {}), qPrintable(store.lastErrorText()));

        const auto runs = store.recentMatchRuns(10);
        QVERIFY(!store.hasReadError());
        QCOMPARE(runs.size(), 2);
        QCOMPARE(runs.at(0).runId, QStringLiteral("newer-run"));
        QCOMPARE(runs.at(1).runId, QStringLiteral("older-run"));

        const auto limited = store.recentMatchRuns(1);
        QVERIFY(!store.hasReadError());
        QCOMPARE(limited.size(), 1);
        QCOMPARE(limited.first().runId, QStringLiteral("newer-run"));
    }

    void rejectsEmptyRunIdWithChineseError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("invalid.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(!store.saveMatchRun(matchRun(QString()), generatedCandidates()));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("匹配运行 ID")));
    }

    void surfacesMatchReadFailures()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString databasePath = dir.filePath(QStringLiteral("match-read-error.sqlite"));

        svm::storage::SessionStore store;
        QVERIFY2(store.open(databasePath), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveMatchRun(matchRun(QStringLiteral("match-read-error")), generatedCandidates()), qPrintable(store.lastErrorText()));

        dropTableForReadFailure(databasePath, QStringLiteral("match_candidates"));

        const auto candidates = store.matchCandidates(QStringLiteral("match-read-error"));
        QVERIFY(candidates.isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取匹配候选失败")));

        dropTableForReadFailure(databasePath, QStringLiteral("match_runs"));

        const auto recentRuns = store.recentMatchRuns();
        QVERIFY(recentRuns.isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取最近匹配运行失败")));

        const auto run = store.matchRun(QStringLiteral("match-read-error"));
        QVERIFY(!run.has_value());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取匹配运行失败")));
    }
};

QTEST_MAIN(MatchPersistenceTests)
#include "match_persistence_tests.moc"
