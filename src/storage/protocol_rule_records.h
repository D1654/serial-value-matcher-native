#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace svm::storage {

struct ProtocolFieldRuleRecord {
    qint64 id = 0;
    QString ruleId;
    QString fieldName;
    QString sourceStabilityRunId;
    qint64 sourceStableCandidateId = 0;
    QString candidateType;
    QString wordOrder;
    QString byteOrder;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    QString unit;
    QString confidenceLevel;
    double stabilityScore = 0.0;
    QString evidenceSummary;
    QString interpretationMap;
    QDateTime createdAtUtc;
};

} // namespace svm::storage
