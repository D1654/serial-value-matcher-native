#include <QtTest/QtTest>
#include <QElapsedTimer>

#include "matching/candidate_stability_analyzer.h"
#include "matching/protocol_rule_verifier.h"
#include "matching/value_candidate_generator.h"
#include "storage/protocol_rule_records.h"

using namespace svm::matching;

namespace {

RegisterSample sample(int address, quint16 value)
{
    RegisterSample result;
    result.observationId = 100000 + address;
    result.sessionId = QStringLiteral("quality-scan");
    result.slaveId = 1;
    result.functionCode = 3;
    result.address = address;
    result.value = value;
    result.blockIndex = address / 64;
    result.attemptIndex = 0;
    result.observedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs).addMSecs(address);
    return result;
}

TargetValue broadTarget()
{
    TargetValue target;
    target.label = QStringLiteral("质量回归目标");
    target.value = 12345.0;
    target.unit = QStringLiteral("unit");
    target.sampledAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs);
    return target;
}

svm::storage::ProtocolFieldRuleRecord uint16Rule(int index)
{
    svm::storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = QStringLiteral("quality-rule-%1").arg(index);
    rule.fieldName = QStringLiteral("字段%1").arg(index);
    rule.sourceStabilityRunId = QStringLiteral("quality-stability");
    rule.sourceStableCandidateId = index;
    rule.candidateType = QStringLiteral("UInt16");
    rule.wordOrder = QStringLiteral("HighWordFirst");
    rule.byteOrder = QStringLiteral("BigEndian");
    rule.slaveId = 1;
    rule.functionCode = 3;
    rule.startAddress = index;
    rule.registerCount = 1;
    rule.scaleMultiplier = 1.0;
    rule.scaleOffset = 0.0;
    rule.confidenceLevel = QStringLiteral("高");
    rule.stabilityScore = 90.0;
    rule.evidenceSummary = QStringLiteral("质量回归规则");
    rule.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs);
    return rule;
}

CandidateObservation stabilityObservation(int runIndex, int candidateIndex)
{
    CandidateObservation observation;
    observation.runId = QStringLiteral("quality-match-%1").arg(runIndex);
    observation.sourceScanSessionId = QStringLiteral("quality-scan-%1").arg(runIndex);
    observation.candidateType = QStringLiteral("UInt16");
    observation.wordOrder = QStringLiteral("HighWordFirst");
    observation.byteOrder = QStringLiteral("BigEndian");
    observation.sourceSessionId = observation.sourceScanSessionId;
    observation.slaveId = 1;
    observation.functionCode = 3;
    observation.startAddress = candidateIndex;
    observation.registerCount = 1;
    observation.scaleMultiplier = 1.0;
    observation.scaleOffset = 0.0;
    observation.targetValue = 1000.0 + runIndex;
    observation.engineeringValue = observation.targetValue + 0.01;
    observation.absoluteError = 0.01;
    observation.effectiveTolerance = 1.0;
    observation.candidateScore = 99.0;
    observation.observationIds = {static_cast<qint64>(runIndex * 10000 + candidateIndex)};
    observation.addresses = {candidateIndex};
    return observation;
}

} // namespace

class QualityRegressionTests final : public QObject {
    Q_OBJECT

private slots:
    void candidateGenerationKeepsBoundOnDenseLargeInput()
    {
        QList<RegisterSample> samples;
        samples.reserve(2048);
        for (int address = 0; address < 2048; ++address) {
            samples.append(sample(address, static_cast<quint16>(address)));
        }

        CandidateGenerationOptions options;
        options.tolerance.absolute = 1000000000.0;
        options.maxCandidates = 40;

        QElapsedTimer timer;
        timer.start();
        const CandidateGenerationResult result = generateValueCandidates(samples, broadTarget(), options);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.candidates.size() <= options.maxCandidates);
        QVERIFY2(timer.elapsed() < 5000, "候选生成大样本回归超过 5 秒，可能存在性能退化。");
    }

    void ruleVerificationHandlesManyRulesWithIndexedSamples()
    {
        QList<RegisterSample> samples;
        QList<svm::storage::ProtocolFieldRuleRecord> rules;
        samples.reserve(4096);
        rules.reserve(1500);
        for (int address = 0; address < 4096; ++address) {
            samples.append(sample(address, static_cast<quint16>(address)));
            if (address < 1500) {
                rules.append(uint16Rule(address));
            }
        }

        QElapsedTimer timer;
        timer.start();
        const ProtocolRuleVerificationSummary summary = verifyProtocolFieldRules(rules, samples);

        QCOMPARE(summary.totalRules, rules.size());
        QCOMPARE(summary.verifiedRules, rules.size());
        QCOMPARE(summary.missingRules, 0);
        QCOMPARE(summary.unsupportedRules, 0);
        QVERIFY2(timer.elapsed() < 3000, "规则验证大样本回归超过 3 秒，索引路径可能退化。");
    }

    void stabilityAnalysisHandlesManyRunsAndCandidateGroups()
    {
        QList<CandidateObservation> observations;
        observations.reserve(2000);
        for (int runIndex = 0; runIndex < 20; ++runIndex) {
            for (int candidateIndex = 0; candidateIndex < 100; ++candidateIndex) {
                observations.append(stabilityObservation(runIndex, candidateIndex));
            }
        }

        StabilityAnalysisOptions options;
        options.minimumSampleCount = 2;
        options.strongSampleCount = 10;

        QElapsedTimer timer;
        timer.start();
        const StabilityAnalysisResult result = analyzeCandidateStability(observations, options);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.candidates.size(), 100);
        QVERIFY(result.candidates.first().sampleCount >= options.strongSampleCount);
        QVERIFY2(timer.elapsed() < 3000, "稳定性分析大样本回归超过 3 秒，分组路径可能退化。");
    }
};

QTEST_MAIN(QualityRegressionTests)
#include "quality_regression_tests.moc"
