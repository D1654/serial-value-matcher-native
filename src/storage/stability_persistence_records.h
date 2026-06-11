#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace svm::storage {

struct StabilityRunRecord {
    QString stabilityRunId;
    QList<QString> sourceMatchRunIds;
    int minimumSampleCount = 2;
    int strongSampleCount = 4;
    int stableCandidateCount = 0;
    QDateTime createdAtUtc;
};

struct StableCandidateRecord {
    qint64 id = 0;
    QString stabilityRunId;
    int rankIndex = 0;
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

} // namespace svm::storage
