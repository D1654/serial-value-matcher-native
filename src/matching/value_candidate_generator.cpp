#include "matching/value_candidate_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace svm::matching {
namespace {

bool isFinite(double value)
{
    return std::isfinite(value);
}

bool sameSeries(const RegisterSample& left, const RegisterSample& right)
{
    return left.sessionId == right.sessionId
        && left.slaveId == right.slaveId
        && left.functionCode == right.functionCode;
}

double effectiveToleranceFor(const TargetValue& target, const MatchTolerance& tolerance)
{
    return std::max(tolerance.absolute, std::abs(target.value) * tolerance.relativeRatio);
}

bool withinTolerance(double absoluteError, double effectiveTolerance)
{
    if (effectiveTolerance <= 0.0) {
        return absoluteError == 0.0;
    }
    return absoluteError <= effectiveTolerance;
}

double typePrior(NumericCandidateType type)
{
    switch (type) {
    case NumericCandidateType::UInt16:
        return 1.00;
    case NumericCandidateType::Int16:
        return 0.98;
    case NumericCandidateType::UInt32:
        return 0.92;
    case NumericCandidateType::Int32:
        return 0.90;
    case NumericCandidateType::Float32:
        return 0.85;
    case NumericCandidateType::PackedBCD:
        return 0.96;
    case NumericCandidateType::Gray16:
        return 0.94;
    case NumericCandidateType::BitFlags:
        return 0.88;
    }
    return 0.80;
}

double endianPrior(WordOrder wordOrder, ByteOrder byteOrder, int registerCount)
{
    if (registerCount == 1) {
        return byteOrder == ByteOrder::BigEndian ? 1.00 : 0.90;
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::BigEndian) {
        return 1.00;
    }
    if (wordOrder == WordOrder::LowWordFirst && byteOrder == ByteOrder::BigEndian) {
        return 0.95;
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::LittleEndian) {
        return 0.90;
    }
    return 0.85;
}

double scalePrior(const ScaleTransform& scale)
{
    if (scale.offset != 0.0) {
        return 0.85;
    }
    if (scale.multiplier == 1.0) {
        return 1.00;
    }
    if (scale.multiplier == 0.1 || scale.multiplier == 0.01 || scale.multiplier == 0.001 || scale.multiplier == 0.0001) {
        return 0.95;
    }
    if (scale.multiplier == 10.0 || scale.multiplier == 100.0 || scale.multiplier == 1000.0) {
        return 0.90;
    }
    return 0.80;
}

double scoreFor(NumericCandidateType type,
                WordOrder wordOrder,
                ByteOrder byteOrder,
                int registerCount,
                const ScaleTransform& scale,
                double absoluteError,
                double effectiveTolerance)
{
    const double matchQuality = effectiveTolerance <= 0.0 ? 1.0 : std::max(0.0, 1.0 - (absoluteError / effectiveTolerance));
    const double baseScore = 60.0 + 40.0 * matchQuality;
    return baseScore * typePrior(type) * endianPrior(wordOrder, byteOrder, registerCount) * scalePrior(scale);
}

void appendSource(ValueMatchCandidate& candidate, const RegisterSample& sample)
{
    candidate.observationIds.append(sample.observationId);
    candidate.addresses.append(sample.address);
    candidate.blockIndexes.append(sample.blockIndex);
    candidate.attemptIndexes.append(sample.attemptIndex);
    candidate.rawRegisters.append(sample.value);
}

bool candidateRankedBefore(const ValueMatchCandidate& left, const ValueMatchCandidate& right);
void appendBoundedCandidate(QList<ValueMatchCandidate>& candidates,
                            const ValueMatchCandidate& candidate,
                            int maxCandidates);

void addCandidate(QList<ValueMatchCandidate>& candidates,
                  int maxCandidates,
                  const TargetValue& target,
                  double effectiveTolerance,
                  NumericCandidateType type,
                  WordOrder wordOrder,
                  ByteOrder byteOrder,
                  const RegisterSample& first,
                  const RegisterSample* second,
                  double decodedValue,
                  const ScaleTransform& scale)
{
    if (!isFinite(decodedValue) || !isFinite(scale.multiplier) || !isFinite(scale.offset) || scale.multiplier == 0.0) {
        return;
    }

    const double engineeringValue = decodedValue * scale.multiplier + scale.offset;
    if (!isFinite(engineeringValue)) {
        return;
    }

    const double delta = engineeringValue - target.value;
    const double absoluteError = std::abs(delta);
    if (!withinTolerance(absoluteError, effectiveTolerance)) {
        return;
    }

    ValueMatchCandidate candidate;
    candidate.type = type;
    candidate.wordOrder = wordOrder;
    candidate.byteOrder = byteOrder;
    candidate.sessionId = first.sessionId;
    candidate.slaveId = first.slaveId;
    candidate.functionCode = first.functionCode;
    candidate.startAddress = first.address;
    candidate.registerCount = second == nullptr ? 1 : 2;
    appendSource(candidate, first);
    if (second != nullptr) {
        appendSource(candidate, *second);
    }
    candidate.decodedValue = decodedValue;
    candidate.scale = scale;
    candidate.engineeringValue = engineeringValue;
    candidate.delta = delta;
    candidate.absoluteError = absoluteError;
    candidate.effectiveTolerance = effectiveTolerance;
    candidate.score = scoreFor(type, wordOrder, byteOrder, candidate.registerCount, scale, absoluteError, effectiveTolerance);
    candidate.observedAtUtc = first.observedAtUtc.isValid() ? first.observedAtUtc : (second != nullptr ? second->observedAtUtc : QDateTime{});
    candidate.evidenceText = QStringLiteral("单样本候选：地址 %1，%2，%3，解码值 %4，工程值 %5，误差 %6")
                                 .arg(QString::number(candidate.startAddress),
                                      numericCandidateTypeName(type),
                                      endianDescription(wordOrder, byteOrder, candidate.registerCount),
                                      QString::number(candidate.decodedValue, 'g', 12),
                                      QString::number(candidate.engineeringValue, 'g', 12),
                                      QString::number(candidate.absoluteError, 'g', 12));
    appendBoundedCandidate(candidates, candidate, maxCandidates);
}

QList<ScaleTransform> normalizedScaleTransforms(const QList<ScaleTransform>& transforms)
{
    QList<ScaleTransform> result;
    for (const ScaleTransform& transform : transforms) {
        if (!isFinite(transform.multiplier) || !isFinite(transform.offset) || transform.multiplier == 0.0) {
            continue;
        }
        const bool exists = std::any_of(result.cbegin(), result.cend(), [&transform](const ScaleTransform& existing) {
            return existing.multiplier == transform.multiplier && existing.offset == transform.offset;
        });
        if (!exists) {
            result.append(transform);
        }
    }
    return result;
}

bool standardEndianBefore(const ValueMatchCandidate& left, const ValueMatchCandidate& right)
{
    const double leftPrior = endianPrior(left.wordOrder, left.byteOrder, left.registerCount);
    const double rightPrior = endianPrior(right.wordOrder, right.byteOrder, right.registerCount);
    return leftPrior > rightPrior;
}

bool candidateRankedBefore(const ValueMatchCandidate& left, const ValueMatchCandidate& right)
{
    if (left.score != right.score) {
        return left.score > right.score;
    }
    if (left.absoluteError != right.absoluteError) {
        return left.absoluteError < right.absoluteError;
    }
    if (left.registerCount != right.registerCount) {
        return left.registerCount < right.registerCount;
    }
    if (standardEndianBefore(left, right) != standardEndianBefore(right, left)) {
        return standardEndianBefore(left, right);
    }
    if (left.startAddress != right.startAddress) {
        return left.startAddress < right.startAddress;
    }
    return numericCandidateTypeName(left.type) < numericCandidateTypeName(right.type);
}

void removeWorstCandidate(QList<ValueMatchCandidate>& candidates)
{
    auto worst = candidates.begin();
    for (auto current = candidates.begin() + 1; current != candidates.end(); ++current) {
        if (candidateRankedBefore(*worst, *current)) {
            worst = current;
        }
    }
    candidates.erase(worst);
}

void appendBoundedCandidate(QList<ValueMatchCandidate>& candidates,
                            const ValueMatchCandidate& candidate,
                            int maxCandidates)
{
    candidates.append(candidate);
    if (candidates.size() > maxCandidates) {
        removeWorstCandidate(candidates);
    }
}

} // namespace

QString numericCandidateTypeName(NumericCandidateType type)
{
    switch (type) {
    case NumericCandidateType::UInt16:
        return QStringLiteral("UInt16");
    case NumericCandidateType::Int16:
        return QStringLiteral("Int16");
    case NumericCandidateType::UInt32:
        return QStringLiteral("UInt32");
    case NumericCandidateType::Int32:
        return QStringLiteral("Int32");
    case NumericCandidateType::Float32:
        return QStringLiteral("Float32");
    case NumericCandidateType::PackedBCD:
        return QStringLiteral("PackedBCD");
    case NumericCandidateType::Gray16:
        return QStringLiteral("Gray16");
    case NumericCandidateType::BitFlags:
        return QStringLiteral("BitFlags");
    }
    return QStringLiteral("未知数值类型");
}

QString wordOrderName(WordOrder order)
{
    switch (order) {
    case WordOrder::HighWordFirst:
        return QStringLiteral("高字在前");
    case WordOrder::LowWordFirst:
        return QStringLiteral("低字在前");
    }
    return QStringLiteral("未知字序");
}

QString byteOrderName(ByteOrder order)
{
    switch (order) {
    case ByteOrder::BigEndian:
        return QStringLiteral("寄存器内高字节在前");
    case ByteOrder::LittleEndian:
        return QStringLiteral("寄存器内低字节在前");
    }
    return QStringLiteral("未知字节序");
}

QString endianDescription(WordOrder wordOrder, ByteOrder byteOrder, int registerCount)
{
    if (registerCount == 1) {
        return byteOrder == ByteOrder::BigEndian ? QStringLiteral("标准字节序") : QStringLiteral("寄存器内字节交换");
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::BigEndian) {
        return QStringLiteral("标准字节序");
    }
    if (wordOrder == WordOrder::LowWordFirst && byteOrder == ByteOrder::BigEndian) {
        return QStringLiteral("word swap");
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::LittleEndian) {
        return QStringLiteral("寄存器内字节交换");
    }
    return QStringLiteral("word swap + 字节交换");
}

CandidateGenerationResult generateValueCandidates(const QList<RegisterSample>& samples,
                                                   const TargetValue& target,
                                                   const CandidateGenerationOptions& options)
{
    CandidateGenerationResult result;

    if (samples.isEmpty()) {
        result.errorMessage = QStringLiteral("没有可用于候选生成的寄存器观测");
        return result;
    }
    if (!isFinite(target.value)) {
        result.errorMessage = QStringLiteral("目标值不是有效数字");
        return result;
    }
    if (!isFinite(options.tolerance.absolute) || options.tolerance.absolute < 0.0
        || !isFinite(options.tolerance.relativeRatio) || options.tolerance.relativeRatio < 0.0) {
        result.errorMessage = QStringLiteral("容差必须是大于等于 0 的有效数字");
        return result;
    }
    if (options.maxCandidates <= 0) {
        result.errorMessage = QStringLiteral("候选数量上限必须大于 0");
        return result;
    }

    const QList<ScaleTransform> scales = normalizedScaleTransforms(options.scaleTransforms);
    if (scales.isEmpty()) {
        result.errorMessage = QStringLiteral("至少需要一个有效倍率");
        return result;
    }

    const double effectiveTolerance = effectiveToleranceFor(target, options.tolerance);
    if (samples.isEmpty()) {
        result.errorMessage = QStringLiteral("没有可用于匹配的寄存器观测");
        return result;
    }

    QList<RegisterSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(), [](const RegisterSample& left, const RegisterSample& right) {
        if (left.sessionId != right.sessionId) {
            return left.sessionId < right.sessionId;
        }
        if (left.slaveId != right.slaveId) {
            return left.slaveId < right.slaveId;
        }
        if (left.functionCode != right.functionCode) {
            return left.functionCode < right.functionCode;
        }
        return left.address < right.address;
    });

    QList<ValueMatchCandidate> candidates;
    const QList<ByteOrder> byteOrders = {ByteOrder::BigEndian, ByteOrder::LittleEndian};
    const QList<WordOrder> wordOrders = {WordOrder::HighWordFirst, WordOrder::LowWordFirst};

    for (qsizetype i = 0; i < sorted.size(); ++i) {
        const RegisterSample& first = sorted.at(i);
        for (ByteOrder byteOrder : byteOrders) {
            const quint16 single = normalizeRegisterBytes(first.value, byteOrder);
            for (const ScaleTransform& scale : scales) {
                if (options.includeUInt16) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::UInt16, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(single), scale);
                }
                if (options.includeInt16) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Int16, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(toInt16(single)), scale);
                }
                if (options.includePackedBCD) {
                    const auto bcdValue = decodePackedBcdWords({first.value}, WordOrder::HighWordFirst, byteOrder);
                    if (bcdValue.has_value()) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::PackedBCD, WordOrder::HighWordFirst, byteOrder, first, nullptr, *bcdValue, scale);
                    }
                }
                if (options.includeGray16) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Gray16, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(gray16ToBinary(single)), scale);
                }
                if (options.includeBitFlags) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::BitFlags, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(single), scale);
                }
            }
        }

        if (i + 1 >= sorted.size()) {
            continue;
        }
        const RegisterSample& second = sorted.at(i + 1);
        if (!sameSeries(first, second) || second.address != first.address + 1) {
            continue;
        }

        for (WordOrder wordOrder : wordOrders) {
            for (ByteOrder byteOrder : byteOrders) {
                const quint32 combined = combineWords(first.value, second.value, wordOrder, byteOrder);
                const double uint32Value = static_cast<double>(combined);
                const double int32Value = static_cast<double>(toInt32(combined));
                const double float32Value = static_cast<double>(bitsToFloat32(combined));
                const auto packedBcdValue = decodePackedBcdWords({first.value, second.value}, wordOrder, byteOrder);
                for (const ScaleTransform& scale : scales) {
                    if (options.includeUInt32) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::UInt32, wordOrder, byteOrder, first, &second, uint32Value, scale);
                    }
                    if (options.includeInt32) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Int32, wordOrder, byteOrder, first, &second, int32Value, scale);
                    }
                    if (options.includeFloat32) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Float32, wordOrder, byteOrder, first, &second, float32Value, scale);
                    }
                    if (options.includePackedBCD && packedBcdValue.has_value()) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::PackedBCD, wordOrder, byteOrder, first, &second, *packedBcdValue, scale);
                    }
                }
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), candidateRankedBefore);

    while (candidates.size() > options.maxCandidates) {
        candidates.removeLast();
    }

    result.success = true;
    result.candidates = candidates;
    return result;
}

} // namespace svm::matching
