#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>

#include "matching/value_candidate_generator.h"
#include "storage/protocol_rule_records.h"

namespace svm::matching {

struct ProtocolRuleVerificationResult {
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
    QList<quint16> rawRegisters;
    double decodedValue = 0.0;
    double engineeringValue = 0.0;
    QDateTime observedAtUtc;
    QString interpretationText;
    QString evidenceText;
};

struct ProtocolRuleVerificationSummary {
    int totalRules = 0;
    int verifiedRules = 0;
    int missingRules = 0;
    int unsupportedRules = 0;
    QList<ProtocolRuleVerificationResult> results;
};

ProtocolRuleVerificationSummary verifyProtocolFieldRules(
    const QList<svm::storage::ProtocolFieldRuleRecord>& rules,
    const QList<RegisterSample>& samples);

} // namespace svm::matching
