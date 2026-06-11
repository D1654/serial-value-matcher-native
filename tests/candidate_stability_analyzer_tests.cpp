#include <QtTest/QtTest>

#include "matching/candidate_stability_analyzer.h"

using namespace svm::matching;

namespace {

CandidateObservation observation(QString runId,
                                 double targetValue,
                                 double engineeringValue,
                                 double absoluteError,
                                 double candidateScore,
                                 QString wordOrder = QStringLiteral("HighWordFirst"),
                                 int startAddress = 100)
{
    CandidateObservation result;
    result.runId = runId;
    result.sourceScanSessionId = QStringLiteral("scan-") + runId;
    result.candidateType = QStringLiteral("Float32");
    result.wordOrder = wordOrder;
    result.byteOrder = QStringLiteral("BigEndian");
    result.sourceSessionId = result.sourceScanSessionId;
    result.slaveId = 1;
    result.functionCode = 3;
    result.startAddress = startAddress;
    result.registerCount = 2;
    result.scaleMultiplier = 1.0;
    result.scaleOffset = 0.0;
    result.targetValue = targetValue;
    result.engineeringValue = engineeringValue;
    result.absoluteError = absoluteError;
    result.effectiveTolerance = 0.10;
    result.candidateScore = candidateScore;
    result.observationIds = {1000 + startAddress, 1001 + startAddress};
    result.addresses = {startAddress, startAddress + 1};
    result.blockIndexes = {0, 0};
    result.attemptIndexes = {0, 0};
    return result;
}

} // namespace

class CandidateStabilityAnalyzerTests final : public QObject {
    Q_OBJECT

private slots:
    void ranksStableMultiSampleCandidateAsHighConfidence()
    {
        QList<CandidateObservation> observations;
        observations.append(observation(QStringLiteral("run-1"), 12.34, 12.34, 0.00, 100.0));
        observations.append(observation(QStringLiteral("run-2"), 20.00, 20.01, 0.01, 96.0));
        observations.append(observation(QStringLiteral("run-3"), 33.30, 33.28, 0.02, 94.0));

        StabilityAnalysisOptions options;
        options.minimumSampleCount = 2;
        options.strongSampleCount = 3;
        const StabilityAnalysisResult result = analyzeCandidateStability(observations, options);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.candidates.size(), 1);
        const StableCandidate& candidate = result.candidates.first();
        QCOMPARE(candidate.sampleCount, 3);
        QVERIFY(candidate.meetsMinimumSampleCount);
        QCOMPARE(candidate.confidenceLevel, QStringLiteral("高"));
        QVERIFY(candidate.stabilityScore > 90.0);
        QCOMPARE(candidate.runIds, QList<QString>({QStringLiteral("run-1"), QStringLiteral("run-2"), QStringLiteral("run-3")}));
        QCOMPARE(candidate.addresses, QList<int>({100, 101}));
        QVERIFY(candidate.evidenceSummary.contains(QStringLiteral("置信等级 高")));
    }

    void keepsSingleSampleAsLowConfidenceEvenWithPerfectScore()
    {
        QList<CandidateObservation> observations;
        observations.append(observation(QStringLiteral("run-1"), 12.34, 12.34, 0.0, 100.0));

        StabilityAnalysisOptions options;
        options.minimumSampleCount = 2;
        const StabilityAnalysisResult result = analyzeCandidateStability(observations, options);

        QVERIFY(result.success);
        QCOMPARE(result.candidates.size(), 1);
        const StableCandidate& candidate = result.candidates.first();
        QCOMPARE(candidate.sampleCount, 1);
        QVERIFY(!candidate.meetsMinimumSampleCount);
        QCOMPARE(candidate.confidenceLevel, QStringLiteral("低"));
        QVERIFY(candidate.stabilityScore <= 59.0);
    }

    void groupsByFieldAndDecodingIdentity()
    {
        QList<CandidateObservation> observations;
        observations.append(observation(QStringLiteral("run-1"), 1.0, 1.0, 0.0, 100.0, QStringLiteral("HighWordFirst"), 100));
        observations.append(observation(QStringLiteral("run-2"), 2.0, 2.0, 0.0, 100.0, QStringLiteral("HighWordFirst"), 100));
        observations.append(observation(QStringLiteral("run-3"), 3.0, 3.0, 0.0, 100.0, QStringLiteral("LowWordFirst"), 100));
        observations.append(observation(QStringLiteral("run-4"), 4.0, 4.0, 0.0, 100.0, QStringLiteral("LowWordFirst"), 100));
        observations.append(observation(QStringLiteral("run-5"), 5.0, 5.0, 0.0, 100.0, QStringLiteral("HighWordFirst"), 120));
        observations.append(observation(QStringLiteral("run-6"), 6.0, 6.0, 0.0, 100.0, QStringLiteral("HighWordFirst"), 120));

        const StabilityAnalysisResult result = analyzeCandidateStability(observations);

        QVERIFY(result.success);
        QCOMPARE(result.candidates.size(), 3);
        QList<QString> identities;
        for (const StableCandidate& candidate : result.candidates) {
            identities.append(QStringLiteral("%1@%2").arg(candidate.wordOrder, QString::number(candidate.startAddress)));
            QCOMPARE(candidate.sampleCount, 2);
        }
        QVERIFY(identities.contains(QStringLiteral("HighWordFirst@100")));
        QVERIFY(identities.contains(QStringLiteral("LowWordFirst@100")));
        QVERIFY(identities.contains(QStringLiteral("HighWordFirst@120")));
    }

    void convertsPersistenceRecordsToCandidateObservations()
    {
        svm::storage::MatchRunRecord run;
        run.runId = QStringLiteral("persisted-run");
        run.sourceScanSessionId = QStringLiteral("scan-source");
        run.targetValue = 42.5;

        svm::storage::MatchCandidateRecord record;
        record.runId = run.runId;
        record.candidateType = QStringLiteral("UInt16");
        record.wordOrder = QStringLiteral("HighWordFirst");
        record.byteOrder = QStringLiteral("BigEndian");
        record.sourceSessionId = QStringLiteral("scan-source");
        record.slaveId = 2;
        record.functionCode = 4;
        record.startAddress = 10;
        record.registerCount = 1;
        record.observationIds = {77};
        record.addresses = {10};
        record.blockIndexes = {3};
        record.attemptIndexes = {1};
        record.scaleMultiplier = 0.1;
        record.scaleOffset = 1.0;
        record.engineeringValue = 42.4;
        record.absoluteError = 0.1;
        record.effectiveTolerance = 0.5;
        record.score = 88.0;

        const QList<CandidateObservation> observations = candidateObservationsFromMatchRecords(run, {record});

        QCOMPARE(observations.size(), 1);
        const CandidateObservation& observation = observations.first();
        QCOMPARE(observation.runId, QStringLiteral("persisted-run"));
        QCOMPARE(observation.sourceScanSessionId, QStringLiteral("scan-source"));
        QCOMPARE(observation.candidateType, QStringLiteral("UInt16"));
        QCOMPARE(observation.slaveId, 2);
        QCOMPARE(observation.functionCode, 4);
        QCOMPARE(observation.startAddress, 10);
        QCOMPARE(observation.targetValue, 42.5);
        QCOMPARE(observation.engineeringValue, 42.4);
        QCOMPARE(observation.observationIds, QList<qint64>({77}));
        QCOMPARE(observation.blockIndexes, QList<int>({3}));
        QCOMPARE(observation.attemptIndexes, QList<int>({1}));
    }

    void rejectsInvalidInputWithChineseMessage()
    {
        StabilityAnalysisResult result = analyzeCandidateStability({});
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("稳定性分析")));

        CandidateObservation invalid = observation(QStringLiteral("run-1"), 1.0, 1.0, 0.0, 100.0);
        invalid.candidateType.clear();
        result = analyzeCandidateStability({invalid});
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("候选类型")));
    }
};

QTEST_MAIN(CandidateStabilityAnalyzerTests)
#include "candidate_stability_analyzer_tests.moc"
