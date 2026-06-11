#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace svm::matching {

struct ProtocolRuleTypeMetadata {
    QString candidateType;
    QString displayName;
    int minRegisterCount = 1;
    int maxRegisterCount = 1;
    bool supportsInterpretationMap = false;
};

QList<ProtocolRuleTypeMetadata> supportedProtocolRuleTypeMetadata();
QStringList supportedProtocolRuleTypeNames();
std::optional<ProtocolRuleTypeMetadata> protocolRuleTypeMetadata(const QString& candidateType);
bool isSupportedProtocolRuleType(const QString& candidateType);
QString registerCountRequirementText(const ProtocolRuleTypeMetadata& metadata);
QString validateProtocolRuleTypeAndRegisterCount(const QString& candidateType, int registerCount);
bool protocolRuleTypeSupportsInterpretationMap(const QString& candidateType);

} // namespace svm::matching
