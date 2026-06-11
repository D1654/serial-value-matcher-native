#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>

#include "matching/numeric_decoder.h"

namespace svm::matching {

struct TargetValue {
    QString label;
    double value = 0.0;
    QString unit;
    QDateTime sampledAtUtc;
};

struct MatchTolerance {
    double absolute = 0.0;
    double relativeRatio = 0.0;
};

struct ScaleTransform {
    double multiplier = 1.0;
    double offset = 0.0;
};

struct RegisterSample {
    qint64 observationId = 0;
    QString sessionId;
    int slaveId = 0;
    int functionCode = 0;
    int address = 0;
    quint16 value = 0;
    int blockIndex = -1;
    int attemptIndex = 0;
    QDateTime observedAtUtc;
};

struct CandidateGenerationOptions {
    bool includeUInt16 = true;
    bool includeInt16 = true;
    bool includeUInt32 = true;
    bool includeInt32 = true;
    bool includeFloat32 = true;
    bool includePackedBCD = true;
    bool includeGray16 = true;
    bool includeBitFlags = true;
    QList<ScaleTransform> scaleTransforms = {{1.0, 0.0}};
    MatchTolerance tolerance;
    int maxCandidates = 200;
};

struct ValueMatchCandidate {
    NumericCandidateType type = NumericCandidateType::UInt16;
    WordOrder wordOrder = WordOrder::HighWordFirst;
    ByteOrder byteOrder = ByteOrder::BigEndian;
    QString sessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    QList<qint64> observationIds;
    QList<int> addresses;
    QList<int> blockIndexes;
    QList<int> attemptIndexes;
    QList<quint16> rawRegisters;
    double decodedValue = 0.0;
    ScaleTransform scale;
    double engineeringValue = 0.0;
    double delta = 0.0;
    double absoluteError = 0.0;
    double effectiveTolerance = 0.0;
    double score = 0.0;
    QDateTime observedAtUtc;
    QString evidenceText;
};

struct CandidateGenerationResult {
    bool success = false;
    QString errorMessage;
    QList<ValueMatchCandidate> candidates;
};

QString numericCandidateTypeName(NumericCandidateType type);
QString wordOrderName(WordOrder order);
QString byteOrderName(ByteOrder order);
QString endianDescription(WordOrder wordOrder, ByteOrder byteOrder, int registerCount);

CandidateGenerationResult generateValueCandidates(const QList<RegisterSample>& samples,
                                                   const TargetValue& target,
                                                   const CandidateGenerationOptions& options = {});

} // namespace svm::matching
