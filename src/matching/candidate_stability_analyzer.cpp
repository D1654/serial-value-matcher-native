#include "matching/candidate_stability_analyzer.h"

#include <algorithm>
#include <cmath>

#include <QMap>
#include <QSet>
#include <QStringList>

namespace svm::matching {

namespace {

bool isFinite(double value)
{
    return std::isfinite(value);
}

double clampScore(double value)
{
    if (!isFinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 100.0);
}

QString identityKey(const CandidateObservation& observation)
{
    return QStringList{
        observation.candidateType,
        observation.wordOrder,
        observation.byteOrder,
        QString::number(observation.slaveId),
        QString::number(observation.functionCode),
        QString::number(observation.startAddress),
        QString::number(observation.registerCount),
        QString::number(observation.scaleMultiplier, 'g', 17),
        QString::number(observation.scaleOffset, 'g', 17),
    }.join(QLatin1Char('|'));
}

template <typename T>
void appendUnique(QList<T>& target, const T& value)
{
    if (!target.contains(value)) {
        target.append(value);
    }
}

double qualityForError(double absoluteError, double effectiveTolerance)
{
    if (!isFinite(absoluteError) || absoluteError < 0.0) {
        return 0.0;
    }
    if (!isFinite(effectiveTolerance) || effectiveTolerance < 0.0) {
        return 0.0;
    }
    if (effectiveTolerance == 0.0) {
        return absoluteError == 0.0 ? 100.0 : 0.0;
    }
    return clampScore(100.0 * (1.0 - (absoluteError / effectiveTolerance)));
}

double qualityForSampleCount(int sampleCount, const StabilityAnalysisOptions& options)
{
    const int minimum = std::max(1, options.minimumSampleCount);
    const int strong = std::max(minimum, options.strongSampleCount);
    if (sampleCount < minimum) {
        return clampScore(30.0 * static_cast<double>(sampleCount) / static_cast<double>(minimum));
    }
    if (strong == minimum) {
        return 100.0;
    }
    const double progress = static_cast<double>(sampleCount - minimum) / static_cast<double>(strong - minimum);
    return clampScore(70.0 + 30.0 * progress);
}

QString confidenceFor(double stabilityScore, int sampleCount, int minimumSampleCount)
{
    if (sampleCount < minimumSampleCount) {
        return QStringLiteral("低");
    }
    if (stabilityScore >= 85.0) {
        return QStringLiteral("高");
    }
    if (stabilityScore >= 65.0) {
        return QStringLiteral("中");
    }
    return QStringLiteral("低");
}

} // namespace

QList<CandidateObservation> candidateObservationsFromMatchRecords(
    const storage::MatchRunRecord& run,
    const QList<storage::MatchCandidateRecord>& candidates)
{
    QList<CandidateObservation> observations;
    observations.reserve(candidates.size());

    for (const storage::MatchCandidateRecord& candidate : candidates) {
        CandidateObservation observation;
        observation.runId = run.runId;
        observation.sourceScanSessionId = run.sourceScanSessionId;
        observation.candidateType = candidate.candidateType;
        observation.wordOrder = candidate.wordOrder;
        observation.byteOrder = candidate.byteOrder;
        observation.sourceSessionId = candidate.sourceSessionId;
        observation.slaveId = candidate.slaveId;
        observation.functionCode = candidate.functionCode;
        observation.startAddress = candidate.startAddress;
        observation.registerCount = candidate.registerCount;
        observation.scaleMultiplier = candidate.scaleMultiplier;
        observation.scaleOffset = candidate.scaleOffset;
        observation.targetValue = run.targetValue;
        observation.engineeringValue = candidate.engineeringValue;
        observation.absoluteError = candidate.absoluteError;
        observation.effectiveTolerance = candidate.effectiveTolerance;
        observation.candidateScore = candidate.score;
        observation.observationIds = candidate.observationIds;
        observation.addresses = candidate.addresses;
        observation.blockIndexes = candidate.blockIndexes;
        observation.attemptIndexes = candidate.attemptIndexes;
        observations.append(observation);
    }

    return observations;
}

StabilityAnalysisResult analyzeCandidateStability(const QList<CandidateObservation>& observations,
                                                   const StabilityAnalysisOptions& options)
{
    StabilityAnalysisResult result;

    if (observations.isEmpty()) {
        result.errorMessage = QStringLiteral("没有可用于稳定性分析的候选观测");
        return result;
    }
    if (options.minimumSampleCount <= 0 || options.strongSampleCount <= 0) {
        result.errorMessage = QStringLiteral("样本数量阈值必须大于 0");
        return result;
    }

    QMap<QString, QList<CandidateObservation>> groups;
    for (const CandidateObservation& observation : observations) {
        if (observation.candidateType.isEmpty()) {
            result.errorMessage = QStringLiteral("候选类型不能为空");
            return result;
        }
        if (!isFinite(observation.targetValue) || !isFinite(observation.engineeringValue)
            || !isFinite(observation.absoluteError) || !isFinite(observation.effectiveTolerance)
            || !isFinite(observation.candidateScore)) {
            result.errorMessage = QStringLiteral("候选观测包含无效数字");
            return result;
        }
        groups[identityKey(observation)].append(observation);
    }

    const int minimum = std::max(1, options.minimumSampleCount);
    for (const QList<CandidateObservation>& group : groups) {
        if (group.isEmpty()) {
            continue;
        }

        const CandidateObservation& first = group.first();
        StableCandidate stable;
        stable.candidateType = first.candidateType;
        stable.wordOrder = first.wordOrder;
        stable.byteOrder = first.byteOrder;
        stable.slaveId = first.slaveId;
        stable.functionCode = first.functionCode;
        stable.startAddress = first.startAddress;
        stable.registerCount = first.registerCount;
        stable.scaleMultiplier = first.scaleMultiplier;
        stable.scaleOffset = first.scaleOffset;
        stable.sampleCount = group.size();
        stable.meetsMinimumSampleCount = stable.sampleCount >= minimum;

        double targetSum = 0.0;
        double engineeringSum = 0.0;
        double errorSum = 0.0;
        double qualitySum = 0.0;
        double scoreSum = 0.0;
        double maxError = 0.0;

        for (const CandidateObservation& observation : group) {
            appendUnique(stable.runIds, observation.runId);
            appendUnique(stable.sourceScanSessionIds, observation.sourceScanSessionId);
            for (const qint64 id : observation.observationIds) {
                appendUnique(stable.observationIds, id);
            }
            for (const int address : observation.addresses) {
                appendUnique(stable.addresses, address);
            }

            targetSum += observation.targetValue;
            engineeringSum += observation.engineeringValue;
            errorSum += observation.absoluteError;
            qualitySum += qualityForError(observation.absoluteError, observation.effectiveTolerance);
            scoreSum += clampScore(observation.candidateScore);
            maxError = std::max(maxError, observation.absoluteError);
        }

        stable.meanTargetValue = targetSum / stable.sampleCount;
        stable.meanEngineeringValue = engineeringSum / stable.sampleCount;
        stable.meanAbsoluteError = errorSum / stable.sampleCount;
        stable.maxAbsoluteError = maxError;
        stable.meanCandidateScore = scoreSum / stable.sampleCount;
        stable.meanErrorQuality = qualitySum / stable.sampleCount;
        stable.sampleQuality = qualityForSampleCount(stable.sampleCount, options);
        stable.stabilityScore = clampScore(0.45 * stable.meanCandidateScore
                                           + 0.40 * stable.meanErrorQuality
                                           + 0.15 * stable.sampleQuality);
        if (!stable.meetsMinimumSampleCount) {
            stable.stabilityScore = std::min(stable.stabilityScore, 59.0);
        }
        stable.confidenceLevel = confidenceFor(stable.stabilityScore, stable.sampleCount, minimum);
        stable.evidenceSummary = QStringLiteral("%1 次样本，平均误差 %2，最大误差 %3，稳定性评分 %4，置信等级 %5")
                                     .arg(QString::number(stable.sampleCount),
                                          QString::number(stable.meanAbsoluteError, 'g', 12),
                                          QString::number(stable.maxAbsoluteError, 'g', 12),
                                          QString::number(stable.stabilityScore, 'f', 1),
                                          stable.confidenceLevel);
        result.candidates.append(stable);
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const StableCandidate& left, const StableCandidate& right) {
        if (left.stabilityScore != right.stabilityScore) {
            return left.stabilityScore > right.stabilityScore;
        }
        if (left.sampleCount != right.sampleCount) {
            return left.sampleCount > right.sampleCount;
        }
        if (left.meanAbsoluteError != right.meanAbsoluteError) {
            return left.meanAbsoluteError < right.meanAbsoluteError;
        }
        return left.startAddress < right.startAddress;
    });

    result.success = true;
    return result;
}

} // namespace svm::matching
