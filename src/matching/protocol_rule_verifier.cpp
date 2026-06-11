#include "matching/protocol_rule_verifier.h"

#include "matching/numeric_decoder.h"
#include "matching/protocol_rule_interpretation.h"
#include "matching/protocol_rule_metadata.h"

#include <cmath>
#include <optional>
#include <QHash>
#include <QStringList>

namespace svm::matching {
namespace {

// Supported protocol rule types and register-count requirements live in protocol_rule_metadata.

struct SampleAddressKey {
    int slaveId = 0;
    int functionCode = 0;
    int address = 0;

    friend bool operator==(const SampleAddressKey&, const SampleAddressKey&) = default;
};

size_t qHash(const SampleAddressKey& key, size_t seed) noexcept
{
    return qHashMulti(seed, key.slaveId, key.functionCode, key.address);
}

using NewestSampleIndex = QHash<SampleAddressKey, RegisterSample>;

bool newerSampleFirst(const RegisterSample& left, const RegisterSample& right)
{
    if (left.observedAtUtc != right.observedAtUtc) {
        return left.observedAtUtc > right.observedAtUtc;
    }
    if (left.blockIndex != right.blockIndex) {
        return left.blockIndex > right.blockIndex;
    }
    if (left.attemptIndex != right.attemptIndex) {
        return left.attemptIndex > right.attemptIndex;
    }
    return left.observationId > right.observationId;
}

NewestSampleIndex buildNewestSampleIndex(const QList<RegisterSample>& samples)
{
    NewestSampleIndex index;
    index.reserve(samples.size());

    for (const RegisterSample& sample : samples) {
        const SampleAddressKey key{sample.slaveId, sample.functionCode, sample.address};
        const auto existing = index.find(key);
        if (existing == index.end() || newerSampleFirst(sample, existing.value())) {
            index.insert(key, sample);
        }
    }

    return index;
}

std::optional<RegisterSample> newestSampleAtAddress(const NewestSampleIndex& index,
                                                    const storage::ProtocolFieldRuleRecord& rule,
                                                    int address)
{
    const auto sample = index.constFind(SampleAddressKey{rule.slaveId, rule.functionCode, address});
    if (sample == index.cend()) {
        return std::nullopt;
    }
    return sample.value();
}

QString registersText(const QList<quint16>& registers)
{
    QStringList parts;
    parts.reserve(registers.size());
    for (quint16 value : registers) {
        parts.append(QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x")));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

ProtocolRuleVerificationSummary verifyProtocolFieldRules(
    const QList<storage::ProtocolFieldRuleRecord>& rules,
    const QList<RegisterSample>& samples)
{
    ProtocolRuleVerificationSummary summary;
    summary.totalRules = rules.size();
    const NewestSampleIndex newestSamples = buildNewestSampleIndex(samples);

    for (const storage::ProtocolFieldRuleRecord& rule : rules) {
        ProtocolRuleVerificationResult result;
        result.ruleId = rule.ruleId;
        result.fieldName = rule.fieldName;
        result.unit = rule.unit;
        result.candidateType = rule.candidateType;
        result.slaveId = rule.slaveId;
        result.functionCode = rule.functionCode;
        result.startAddress = rule.startAddress;
        result.registerCount = rule.registerCount;

        const QString metadataError = validateProtocolRuleTypeAndRegisterCount(rule.candidateType, rule.registerCount);
        if (!metadataError.isEmpty()) {
            result.statusText = metadataError;
            result.evidenceText = result.statusText;
            ++summary.unsupportedRules;
            summary.results.append(result);
            continue;
        }

        QList<RegisterSample> matchedSamples;
        bool missing = false;
        int missingAddress = rule.startAddress;
        for (int offset = 0; offset < rule.registerCount; ++offset) {
            const int address = rule.startAddress + offset;
            const auto sample = newestSampleAtAddress(newestSamples, rule, address);
            if (!sample.has_value()) {
                missing = true;
                missingAddress = address;
                break;
            }
            matchedSamples.append(*sample);
        }
        if (missing) {
            result.statusText = QStringLiteral("缺少地址 %1 的观测，无法验证。")
                .arg(missingAddress);
            result.evidenceText = result.statusText;
            ++summary.missingRules;
            summary.results.append(result);
            continue;
        }

        QList<quint16> registers;
        registers.reserve(matchedSamples.size());
        for (const RegisterSample& sample : matchedSamples) {
            result.observationIds.append(sample.observationId);
            registers.append(sample.value);
            if (result.sourceScanSessionId.isEmpty()) {
                result.sourceScanSessionId = sample.sessionId;
            }
            if (!result.observedAtUtc.isValid() || sample.observedAtUtc > result.observedAtUtc) {
                result.observedAtUtc = sample.observedAtUtc;
            }
        }
        result.rawRegisters = registers;

        const auto decodedValue = decodeNumericValue(rule.candidateType, rule.wordOrder, rule.byteOrder, registers);
        if (!decodedValue.has_value() || !std::isfinite(*decodedValue)
            || !std::isfinite(rule.scaleMultiplier) || !std::isfinite(rule.scaleOffset)) {
            result.statusText = QStringLiteral("规则解码失败，无法得到有效工程值。");
            result.evidenceText = result.statusText;
            ++summary.unsupportedRules;
            summary.results.append(result);
            continue;
        }

        result.decodedValue = *decodedValue;
        result.engineeringValue = result.decodedValue * rule.scaleMultiplier + rule.scaleOffset;
        if (!std::isfinite(result.engineeringValue)) {
            result.statusText = QStringLiteral("规则解码失败，工程值不是有效数字。");
            result.evidenceText = result.statusText;
            ++summary.unsupportedRules;
            summary.results.append(result);
            continue;
        }

        if (rule.candidateType == QStringLiteral("BitFlags")) {
            result.interpretationText = bitFlagInterpretationText(rule.interpretationMap, static_cast<quint16>(static_cast<qulonglong>(result.decodedValue) & 0xFFFFu));
        } else if (rule.candidateType == QStringLiteral("EnumMap")) {
            result.interpretationText = enumMapInterpretationText(rule.interpretationMap, static_cast<int>(result.decodedValue));
        }

        result.verified = true;
        result.statusText = QStringLiteral("已验证");
        result.evidenceText = QStringLiteral("字段“%1”按 %2、%3/%4 从地址 %5 解码，原始寄存器 %6，工程值 %7%8。")
            .arg(rule.fieldName,
                 rule.candidateType,
                 rule.wordOrder,
                 rule.byteOrder)
            .arg(rule.startAddress)
            .arg(registersText(registers))
            .arg(QString::number(result.engineeringValue, 'g', 12), rule.unit);
        if (!result.interpretationText.isEmpty()) {
            result.evidenceText.append(QStringLiteral(" %1").arg(result.interpretationText));
        }
        ++summary.verifiedRules;
        summary.results.append(result);
    }

    return summary;
}

} // namespace svm::matching
