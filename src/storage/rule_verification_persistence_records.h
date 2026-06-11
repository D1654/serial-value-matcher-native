#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace svm::storage {

struct RuleVerificationRunRecord {
    qint64 id = 0;
    QString verificationRunId;
    QString sourceScanSessionId;
    int ruleCount = 0;
    int verifiedCount = 0;
    int missingCount = 0;
    int unsupportedCount = 0;
    QDateTime createdAtUtc;
};

struct RuleVerificationResultRecord {
    qint64 id = 0;
    QString verificationRunId;
    QString ruleId;
    QString fieldName;
    QString unit;
    QString candidateType;
    QString sourceScanSessionId;
    bool verified = false;
    QString statusText;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    QList<qint64> observationIds;
    QList<int> rawRegisters;
    double decodedValue = 0.0;
    double engineeringValue = 0.0;
    QDateTime observedAtUtc;
    QString interpretationText;
    QString evidenceText;
};

} // namespace svm::storage
