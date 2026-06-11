#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "matching/candidate_stability_analyzer.h"
#include "storage/session_store.h"

using namespace svm::matching;

namespace {

CandidateObservation observation(QString runId, double targetValue, double engineeringValue, double absoluteError, double score)
{
    CandidateObservation result;
    result.runId = runId;
    result.sourceScanSessionId = QStringLiteral("scan-") + runId;
    result.candidateType = QStringLiteral("Float32");
    result.wordOrder = QStringLiteral("HighWordFirst");
    result.byteOrder = QStringLiteral("BigEndian");
    result.sourceSessionId = result.sourceScanSessionId;
    result.slaveId = 1;
    result.functionCode = 3;
    result.startAddress = 100;
    result.registerCount = 2;
    result.scaleMultiplier = 1.0;
    result.scaleOffset = 0.0;
    result.targetValue = targetValue;
    result.engineeringValue = engineeringValue;
    result.absoluteError = absoluteError;
    result.effectiveTolerance = 0.10;
    result.candidateScore = score;
    result.observationIds = {7000 + runId.right(1).toInt(), 7100 + runId.right(1).toInt()};
    result.addresses = {100, 101};
    return result;
}

QList<StableCandidate> stableCandidates()
{
    QList<CandidateObservation> observations;
    observations.append(observation(QStringLiteral("run-1"), 12.34, 12.34, 0.00, 100.0));
    observations.append(observation(QStringLiteral("run-2"), 20.00, 20.01, 0.01, 96.0));
    observations.append(observation(QStringLiteral("run-3"), 33.30, 33.28, 0.02, 94.0));

    StabilityAnalysisOptions options;
    options.minimumSampleCount = 2;
    options.strongSampleCount = 3;
    const StabilityAnalysisResult result = analyzeCandidateStability(observations, options);
    Q_ASSERT(result.success);
    Q_ASSERT(result.candidates.size() == 1);
    return result.candidates;
}

svm::storage::StabilityRunRecord stabilityRun(QString runId = QStringLiteral("stability-run-1"))
{
    svm::storage::StabilityRunRecord run;
    run.stabilityRunId = runId;
    run.sourceMatchRunIds = {QStringLiteral("run-1"), QStringLiteral("run-2"), QStringLiteral("run-3")};
    run.minimumSampleCount = 2;
    run.strongSampleCount = 3;
    run.createdAtUtc = QDateTime::currentDateTimeUtc();
    return run;
}

} // namespace

class StabilityPersistenceTests final : public QObject {
    Q_OBJECT

private slots:
    void savesAndReloadsStabilityRunWithStableCandidates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("stability.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY2(store.saveStabilityRun(stabilityRun(), stableCandidates()), qPrintable(store.lastErrorText()));

        const auto latestRun = store.latestStabilityRun();
        QVERIFY(latestRun.has_value());
        QCOMPARE(latestRun->stabilityRunId, QStringLiteral("stability-run-1"));

        const auto run = store.stabilityRun(QStringLiteral("stability-run-1"));
        QVERIFY(run.has_value());
        QCOMPARE(run->stabilityRunId, QStringLiteral("stability-run-1"));
        QCOMPARE(run->sourceMatchRunIds, QList<QString>({QStringLiteral("run-1"), QStringLiteral("run-2"), QStringLiteral("run-3")}));
        QCOMPARE(run->minimumSampleCount, 2);
        QCOMPARE(run->strongSampleCount, 3);
        QCOMPARE(run->stableCandidateCount, 1);
        QVERIFY(run->createdAtUtc.isValid());

        const auto candidates = store.stableCandidates(QStringLiteral("stability-run-1"));
        QCOMPARE(candidates.size(), 1);
        const svm::storage::StableCandidateRecord& candidate = candidates.first();
        QCOMPARE(candidate.stabilityRunId, QStringLiteral("stability-run-1"));
        QCOMPARE(candidate.rankIndex, 0);
        QCOMPARE(candidate.candidateType, QStringLiteral("Float32"));
        QCOMPARE(candidate.wordOrder, QStringLiteral("HighWordFirst"));
        QCOMPARE(candidate.byteOrder, QStringLiteral("BigEndian"));
        QCOMPARE(candidate.slaveId, 1);
        QCOMPARE(candidate.functionCode, 3);
        QCOMPARE(candidate.startAddress, 100);
        QCOMPARE(candidate.registerCount, 2);
        QCOMPARE(candidate.sampleCount, 3);
        QVERIFY(candidate.meetsMinimumSampleCount);
        QCOMPARE(candidate.confidenceLevel, QStringLiteral("高"));
        QCOMPARE(candidate.runIds, QList<QString>({QStringLiteral("run-1"), QStringLiteral("run-2"), QStringLiteral("run-3")}));
        QCOMPARE(candidate.sourceScanSessionIds, QList<QString>({QStringLiteral("scan-run-1"), QStringLiteral("scan-run-2"), QStringLiteral("scan-run-3")}));
        QCOMPARE(candidate.addresses, QList<int>({100, 101}));
        QVERIFY(candidate.observationIds.contains(7001));
        QVERIFY(candidate.observationIds.contains(7103));
        QVERIFY(candidate.meanAbsoluteError >= 0.0);
        QVERIFY(candidate.maxAbsoluteError >= candidate.meanAbsoluteError);
        QVERIFY(candidate.stabilityScore > 90.0);
        QVERIFY(candidate.evidenceSummary.contains(QStringLiteral("置信等级 高")));
    }

    void replacesStableCandidatesWhenSavingSameRunAgain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("replace-stability.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY2(store.saveStabilityRun(stabilityRun(QStringLiteral("replace-stability")), stableCandidates()), qPrintable(store.lastErrorText()));
        QCOMPARE(store.stableCandidates(QStringLiteral("replace-stability")).size(), 1);

        svm::storage::StabilityRunRecord rerun = stabilityRun(QStringLiteral("replace-stability"));
        rerun.sourceMatchRunIds = {QStringLiteral("run-4")};
        QVERIFY2(store.saveStabilityRun(rerun, {}), qPrintable(store.lastErrorText()));

        const auto run = store.stabilityRun(QStringLiteral("replace-stability"));
        QVERIFY(run.has_value());
        QCOMPARE(run->sourceMatchRunIds, QList<QString>({QStringLiteral("run-4")}));
        QCOMPARE(run->stableCandidateCount, 0);
        QVERIFY(store.stableCandidates(QStringLiteral("replace-stability")).isEmpty());
    }

    void rejectsEmptyStabilityRunIdWithChineseError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("invalid-stability.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(!store.saveStabilityRun(stabilityRun(QString()), stableCandidates()));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("稳定性运行 ID")));
    }
};

QTEST_MAIN(StabilityPersistenceTests)
#include "stability_persistence_tests.moc"
