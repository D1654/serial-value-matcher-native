#include "matching/protocol_rule_metadata.h"

namespace svm::matching {
namespace {

const QList<ProtocolRuleTypeMetadata>& metadataTable()
{
    static const QList<ProtocolRuleTypeMetadata> table = {
        {QStringLiteral("UInt16"), QStringLiteral("UInt16"), 1, 1, false},
        {QStringLiteral("Int16"), QStringLiteral("Int16"), 1, 1, false},
        {QStringLiteral("UInt32"), QStringLiteral("UInt32"), 2, 2, false},
        {QStringLiteral("Int32"), QStringLiteral("Int32"), 2, 2, false},
        {QStringLiteral("Float32"), QStringLiteral("Float32"), 2, 2, false},
        {QStringLiteral("PackedBCD"), QStringLiteral("PackedBCD"), 1, 2, false},
        {QStringLiteral("Gray16"), QStringLiteral("Gray16"), 1, 1, false},
        {QStringLiteral("BitFlags"), QStringLiteral("BitFlags"), 1, 1, true},
        {QStringLiteral("EnumMap"), QStringLiteral("EnumMap"), 1, 1, true},
    };
    return table;
}

} // namespace

QList<ProtocolRuleTypeMetadata> supportedProtocolRuleTypeMetadata()
{
    return metadataTable();
}

QStringList supportedProtocolRuleTypeNames()
{
    QStringList names;
    names.reserve(metadataTable().size());
    for (const ProtocolRuleTypeMetadata& metadata : metadataTable()) {
        names.append(metadata.candidateType);
    }
    return names;
}

std::optional<ProtocolRuleTypeMetadata> protocolRuleTypeMetadata(const QString& candidateType)
{
    const QString normalized = candidateType.trimmed();
    for (const ProtocolRuleTypeMetadata& metadata : metadataTable()) {
        if (metadata.candidateType == normalized) {
            return metadata;
        }
    }
    return std::nullopt;
}

bool isSupportedProtocolRuleType(const QString& candidateType)
{
    return protocolRuleTypeMetadata(candidateType).has_value();
}

QString registerCountRequirementText(const ProtocolRuleTypeMetadata& metadata)
{
    if (metadata.minRegisterCount == metadata.maxRegisterCount) {
        return QStringLiteral("%1 个寄存器").arg(metadata.minRegisterCount);
    }
    return QStringLiteral("%1-%2 个寄存器").arg(metadata.minRegisterCount).arg(metadata.maxRegisterCount);
}

QString validateProtocolRuleTypeAndRegisterCount(const QString& candidateType, int registerCount)
{
    const auto metadata = protocolRuleTypeMetadata(candidateType);
    if (!metadata.has_value()) {
        return QStringLiteral("规则类型暂不支持：%1").arg(candidateType.trimmed().isEmpty() ? QStringLiteral("<空>") : candidateType.trimmed());
    }
    if (registerCount < metadata->minRegisterCount || registerCount > metadata->maxRegisterCount) {
        return QStringLiteral("规则类型 %1 需要 %2，当前规则为 %3 个寄存器。")
            .arg(metadata->displayName, registerCountRequirementText(*metadata))
            .arg(registerCount);
    }
    return {};
}

bool protocolRuleTypeSupportsInterpretationMap(const QString& candidateType)
{
    const auto metadata = protocolRuleTypeMetadata(candidateType);
    return metadata.has_value() && metadata->supportsInterpretationMap;
}

} // namespace svm::matching
