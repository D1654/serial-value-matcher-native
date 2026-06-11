#pragma once

#include <QList>
#include <QString>

#include "storage/match_persistence_records.h"

namespace svm::matching {

struct CandidateObservation {
    QString runId;
    QString sourceScanSessionId;
    QString candidateType;
    QString wordOrder;
    QString byteOrder;
    QString sourceSessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    double targetValue = 0.0;
    double engineeringValue = 0.0;
    double absoluteError = 0.0;
    double effectiveTolerance = 0.0;
    double candidateScore = 0.0;
    QList<qint64> observationIds;
    QList<int> addresses;
    QList<int> blockIndexes;
    QList<int> attemptIndexes;
};

struct StabilityAnalysisOptions {
    int minimumSampleCount = 2;
    int strongSampleCount = 4;
};

struct StableCandidate {
    QString candidateType;
    QString wordOrder;
    QString byteOrder;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    int sampleCount = 0;
    bool meetsMinimumSampleCount = false;
    QString confidenceLevel;
    QList<QString> runIds;
    QList<QString> sourceScanSessionIds;
    QList<qint64> observationIds;
    QList<int> addresses;
    double meanTargetValue = 0.0;
    double meanEngineeringValue = 0.0;
    double meanAbsoluteError = 0.0;
    double maxAbsoluteError = 0.0;
    double meanCandidateScore = 0.0;
    double meanErrorQuality = 0.0;
    double sampleQuality = 0.0;
    double stabilityScore = 0.0;
    QString evidenceSummary;
};

struct StabilityAnalysisResult {
    bool success = false;
    QString errorMessage;
    QList<StableCandidate> candidates;
};

QList<CandidateObservation> candidateObservationsFromMatchRecords(
    const storage::MatchRunRecord& run,
    const QList<storage::MatchCandidateRecord>& candidates);

StabilityAnalysisResult analyzeCandidateStability(const QList<CandidateObservation>& observations,
                                                   const StabilityAnalysisOptions& options = {});

} // namespace svm::matching
