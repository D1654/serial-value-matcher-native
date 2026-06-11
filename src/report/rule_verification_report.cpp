#include "report/rule_verification_report.h"

#include <QStringList>

namespace svm::report {
namespace {

QString escapeMarkdownCell(QString text)
{
    text.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    text.replace(QLatin1Char('\r'), QString());
    return text;
}

QString formatTime(const QDateTime& value)
{
    return value.isValid()
        ? value.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QStringLiteral("未知");
}

QString formatRegisters(const QList<int>& registers)
{
    if (registers.isEmpty()) {
        return QStringLiteral("-");
    }
    QStringList parts;
    parts.reserve(registers.size());
    for (int value : registers) {
        parts.append(QStringLiteral("0x%1").arg(value & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x")));
    }
    return parts.join(QStringLiteral(", "));
}

QString formatObservationIds(const QList<qint64>& ids)
{
    if (ids.isEmpty()) {
        return QStringLiteral("-");
    }
    QStringList parts;
    parts.reserve(ids.size());
    for (qint64 id : ids) {
        parts.append(QString::number(id));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

QString renderRuleVerificationMarkdownReport(
    const storage::RuleVerificationRunRecord& run,
    const QList<storage::RuleVerificationResultRecord>& results)
{
    QStringList lines;
    lines.append(QStringLiteral("# 协议规则验证报告"));
    lines.append(QString());
    lines.append(QStringLiteral("## 验证摘要"));
    lines.append(QString());
    lines.append(QStringLiteral("- 验证运行 ID：%1").arg(run.verificationRunId));
    lines.append(QStringLiteral("- 来源扫描会话：%1").arg(run.sourceScanSessionId));
    lines.append(QStringLiteral("- 创建时间：%1").arg(formatTime(run.createdAtUtc)));
    lines.append(QStringLiteral("- 总规则数：%1").arg(run.ruleCount));
    lines.append(QStringLiteral("- 已验证：%1").arg(run.verifiedCount));
    lines.append(QStringLiteral("- 缺少观测：%1").arg(run.missingCount));
    lines.append(QStringLiteral("- 暂不支持/失败：%1").arg(run.unsupportedCount));
    lines.append(QString());
    lines.append(QStringLiteral("## 规则验证明细"));
    lines.append(QString());

    if (results.isEmpty()) {
        lines.append(QStringLiteral("暂无验证明细。"));
        lines.append(QString());
        return lines.join(QLatin1Char('\n'));
    }

    lines.append(QStringLiteral("| 结果 | 字段 | 工程值 | 单位 | 从站/功能码 | 地址 | 原始寄存器 | Observation IDs | 解释 | 证据 |"));
    lines.append(QStringLiteral("| --- | --- | ---: | --- | --- | --- | --- | --- | --- | --- |"));
    for (const storage::RuleVerificationResultRecord& result : results) {
        const QString status = result.verified ? QStringLiteral("已验证") : result.statusText;
        const QString engineeringValue = result.verified ? QString::number(result.engineeringValue, 'g', 12) : QStringLiteral("-");
        const QString slaveFunction = QStringLiteral("从站 %1 / FC%2").arg(result.slaveId).arg(result.functionCode);
        const QString address = QStringLiteral("%1（%2 个寄存器）").arg(result.startAddress).arg(result.registerCount);
        lines.append(QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 | %10 |")
            .arg(escapeMarkdownCell(status),
                 escapeMarkdownCell(result.fieldName),
                 escapeMarkdownCell(engineeringValue),
                 escapeMarkdownCell(result.unit),
                 escapeMarkdownCell(slaveFunction),
                 escapeMarkdownCell(address),
                 escapeMarkdownCell(formatRegisters(result.rawRegisters)),
                 escapeMarkdownCell(formatObservationIds(result.observationIds)),
                 escapeMarkdownCell(result.interpretationText.isEmpty() ? QStringLiteral("-") : result.interpretationText),
                 escapeMarkdownCell(result.evidenceText)));
    }
    lines.append(QString());
    lines.append(QStringLiteral("## 说明"));
    lines.append(QString());
    lines.append(QStringLiteral("- “缺少观测”表示所选扫描会话没有覆盖规则所需的地址，不等同于设备异常。"));
    lines.append(QStringLiteral("- “暂不支持/失败”表示当前规则类型或解码结果暂不能生成有效工程值，后续可通过扩展规则类型补齐。"));
    lines.append(QStringLiteral("- 本报告基于已确认规则和扫描观测生成，不会修改原始扫描、候选或规则。"));
    lines.append(QString());
    return lines.join(QLatin1Char('\n'));
}

} // namespace svm::report
