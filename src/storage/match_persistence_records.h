#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>

namespace svm::storage {

struct MatchRunRecord {
    QString runId;
    QString sourceScanSessionId;
    QString targetLabel;
    double targetValue = 0.0;
    QString targetUnit;
    QDateTime sampledAtUtc;
    double toleranceAbsolute = 0.0;
    double toleranceRelativeRatio = 0.0;
    int candidateCount = 0;
    QDateTime createdAtUtc;
};

struct MatchCandidateRecord {
    qint64 id = 0;
    QString runId;
    int rankIndex = 0;
    QString candidateType;
    QString wordOrder;
    QString byteOrder;
    QString sourceSessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    QList<qint64> observationIds;
    QList<int> addresses;
    QList<int> blockIndexes;
    QList<int> attemptIndexes;
    QList<int> rawRegisters;
    double decodedValue = 0.0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    double engineeringValue = 0.0;
    double delta = 0.0;
    double absoluteError = 0.0;
    double effectiveTolerance = 0.0;
    double score = 0.0;
    QDateTime observedAtUtc;
    QString evidenceText;
};

} // namespace svm::storage
