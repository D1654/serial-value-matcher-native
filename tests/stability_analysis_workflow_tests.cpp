#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "analysis/stability_analysis_workflow.h"
#include "matching/value_candidate_generator.h"
#include "storage/session_store.h"

namespace {

svm::matching::RegisterSample sample(int runIndex, int address, quint16 value)
{
    svm::matching::RegisterSample result;
    result.observationId = runIndex * 1000 + address;
    result.sessionId = QStringLiteral("workflow-scan-%1").arg(runIndex);
    result.slaveId = 1;
    result.functionCode = 3;
    result.address = address;
    result.value = value;
    result.blockIndex = 0;
    result.attemptIndex = 0;
    result.observedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs).addSecs(runIndex);
    return result;
}

svm::matching::TargetValue target(double value)
{
    svm::matching::TargetValue result;
    result.label = QStringLiteral("工作流目标");
    result.value = value;
    result.unit = QStringLiteral("unit");
    result.sampledAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs);
    return result;
}

svm::matching::CandidateGenerationOptions floatOnlyOptions()
{
    svm::matching::CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = true;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;
    options.tolerance.absolute = 0.001;
    return options;
}

svm::storage::MatchRunRecord matchRun(int runIndex)
{
    svm::storage::MatchRunRecord run;
    run.runId = QStringLiteral("workflow-match-%1").arg(runIndex);
    run.sourceScanSessionId = QStringLiteral("workflow-scan-%1").arg(runIndex);
    run.targetLabel = QStringLiteral("工作流目标");
    run.targetValue = 12.34;
    run.targetUnit = QStringLiteral("unit");
    run.sampledAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs).addSecs(runIndex);
    run.toleranceAbsolute = 0.001;
    run.createdAtUtc = run.sampledAtUtc;
    return run;
}

QList<svm::matching::ValueMatchCandidate> generatedCandidates(int runIndex)
{
    QList<svm::matching::RegisterSample> samples;
    samples.append(sample(runIndex, 100, 0x4145));
    samples.append(sample(runIndex, 101, 0x70A4));

    const auto result = svm::matching::generateValueCandidates(samples, target(12.34), floatOnlyOptions());
    Q_ASSERT(result.success);
    Q_ASSERT(result.candidates.size() == 1);
    return result.candidates;
}

} // namespace

class StabilityAnalysisWorkflowTests final : public QObject {
    Q_OBJECT

private slots:
    void savesStabilityRunFromRecentMatchRuns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("workflow.sqlite"))), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveMatchRun(matchRun(1), generatedCandidates(1)), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveMatchRun(matchRun(2), generatedCandidates(2)), qPrintable(store.lastErrorText()));

        svm::analysis::StabilityWorkflowOptions options;
        options.minimumSampleCount = 2;
        options.strongSampleCount = 2;
        const auto result = svm::analysis::runRecentMatchStabilityAnalysis(store, options);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.stabilityRunId.startsWith(QStringLiteral("stability-")));
        QCOMPARE(result.sourceMatchRunCount, 2);
        QCOMPARE(result.stableCandidateCount, 1);

        const auto latestRun = store.latestStabilityRun();
        QVERIFY(latestRun.has_value());
        QCOMPARE(latestRun->stabilityRunId, result.stabilityRunId);
        QCOMPARE(latestRun->sourceMatchRunIds.size(), 2);

        const auto candidates = store.stableCandidates(result.stabilityRunId);
        QVERIFY(!store.hasReadError());
        QCOMPARE(candidates.size(), 1);
        QVERIFY(candidates.first().meetsMinimumSampleCount);
    }

    void reportsInsufficientMatchRuns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("workflow-insufficient.sqlite"))), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveMatchRun(matchRun(1), generatedCandidates(1)), qPrintable(store.lastErrorText()));

        svm::analysis::StabilityWorkflowOptions options;
        options.minimumSampleCount = 2;
        const auto result = svm::analysis::runRecentMatchStabilityAnalysis(store, options);

        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QStringLiteral("匹配运行不足")));
    }
};

QTEST_MAIN(StabilityAnalysisWorkflowTests)
#include "stability_analysis_workflow_tests.moc"
