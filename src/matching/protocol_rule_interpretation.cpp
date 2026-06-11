#include "matching/protocol_rule_interpretation.h"

#include <algorithm>
#include <QSet>

namespace svm::matching {
namespace {

QString bitLabel(int bitIndex)
{
    return QStringLiteral("bit%1").arg(bitIndex);
}

QStringList previewHead(const QStringList& parts, int maxCount = 6)
{
    QStringList preview;
    for (int index = 0; index < parts.size() && index < maxCount; ++index) {
        preview.append(parts.at(index));
    }
    if (parts.size() > maxCount) {
        preview.append(QStringLiteral("……共 %1 项").arg(parts.size()));
    }
    return preview;
}

bool parseLineKey(const QString& keyText, int* value)
{
    bool ok = false;
    const int parsed = keyText.trimmed().toInt(&ok, 0);
    if (ok && value != nullptr) {
        *value = parsed;
    }
    return ok;
}

QString bitKeyText(QString keyText)
{
    keyText = keyText.trimmed();
    if (keyText.startsWith(QStringLiteral("bit"), Qt::CaseInsensitive)) {
        keyText = keyText.mid(3).trimmed();
    }
    return keyText;
}

} // namespace

QStringList interpretationMapLines(QString text)
{
    text.replace(QLatin1Char(';'), QLatin1Char('\n'));
    QStringList lines;
    for (const QString& rawLine : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString line = rawLine.trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
            lines.append(line);
        }
    }
    return lines;
}

QList<BitFlagInterpretationDefinition> parseBitFlagInterpretationDefinitions(const QString& text)
{
    QList<BitFlagInterpretationDefinition> definitions;
    for (const QString& line : interpretationMapLines(text)) {
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        int bitIndex = -1;
        if (!parseLineKey(bitKeyText(line.left(separator)), &bitIndex) || bitIndex < 0 || bitIndex > 15) {
            continue;
        }

        const QStringList parts = line.mid(separator + 1).split(QLatin1Char('|'), Qt::KeepEmptyParts);
        BitFlagInterpretationDefinition definition;
        definition.bitIndex = bitIndex;
        definition.name = parts.value(0).trimmed();
        definition.inactiveText = parts.value(1).trimmed();
        definition.activeText = parts.value(2).trimmed();
        if (definition.name.isEmpty()) {
            definition.name = bitLabel(bitIndex);
        }
        if (definition.inactiveText.isEmpty()) {
            definition.inactiveText = QStringLiteral("未置位");
        }
        if (definition.activeText.isEmpty()) {
            definition.activeText = QStringLiteral("置位");
        }
        definitions.append(definition);
    }

    std::sort(definitions.begin(), definitions.end(), [](const BitFlagInterpretationDefinition& left, const BitFlagInterpretationDefinition& right) {
        return left.bitIndex < right.bitIndex;
    });
    return definitions;
}

QList<EnumMapInterpretationDefinition> parseEnumMapInterpretationDefinitions(const QString& text)
{
    QList<EnumMapInterpretationDefinition> definitions;
    for (const QString& line : interpretationMapLines(text)) {
        const qsizetype separator = line.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            continue;
        }

        int value = 0;
        if (!parseLineKey(line.left(separator), &value)) {
            continue;
        }

        const QString label = line.mid(separator + 1).trimmed();
        if (label.isEmpty()) {
            continue;
        }

        EnumMapInterpretationDefinition definition;
        definition.value = value;
        definition.label = label;
        definitions.append(definition);
    }

    std::sort(definitions.begin(), definitions.end(), [](const EnumMapInterpretationDefinition& left, const EnumMapInterpretationDefinition& right) {
        return left.value < right.value;
    });
    return definitions;
}

QString bitFlagInterpretationText(const QString& text, quint16 mask)
{
    const QList<BitFlagInterpretationDefinition> definitions = parseBitFlagInterpretationDefinitions(text);
    if (definitions.isEmpty()) {
        return QString();
    }

    QStringList parts;
    parts.reserve(definitions.size());
    for (const BitFlagInterpretationDefinition& definition : definitions) {
        const bool active = ((mask >> definition.bitIndex) & 0x0001u) != 0u;
        parts.append(QStringLiteral("bit%1 %2=%3")
            .arg(definition.bitIndex)
            .arg(definition.name, active ? definition.activeText : definition.inactiveText));
    }
    return QStringLiteral("位解释：%1。").arg(parts.join(QStringLiteral("；")));
}

QString enumMapInterpretationText(const QString& text, int value)
{
    if (text.trimmed().isEmpty()) {
        return QString();
    }

    const QList<EnumMapInterpretationDefinition> definitions = parseEnumMapInterpretationDefinitions(text);
    for (const EnumMapInterpretationDefinition& definition : definitions) {
        if (definition.value == value) {
            return QStringLiteral("枚举解释：%1。").arg(definition.label);
        }
    }

    return definitions.isEmpty() ? QString() : QStringLiteral("枚举解释：未定义枚举值 %1。").arg(value);
}

InterpretationMapValidationResult validateInterpretationMap(const QString& candidateType, const QString& text)
{
    InterpretationMapValidationResult result;
    const QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        result.previewText = QStringLiteral("未填写解释映射；验证时只显示原始数值。");
        return result;
    }

    if (candidateType == QStringLiteral("BitFlags")) {
        QSet<int> seenBits;
        QStringList previewParts;
        for (const QString& line : interpretationMapLines(trimmedText)) {
            const qsizetype separator = line.indexOf(QLatin1Char('='));
            if (separator <= 0) {
                result.errors.append(QStringLiteral("BitFlags 映射缺少等号：%1").arg(line));
                continue;
            }

            int bitIndex = -1;
            if (!parseLineKey(bitKeyText(line.left(separator)), &bitIndex) || bitIndex < 0 || bitIndex > 15) {
                result.errors.append(QStringLiteral("BitFlags 位号必须是 0-15：%1").arg(line));
                continue;
            }
            if (seenBits.contains(bitIndex)) {
                result.errors.append(QStringLiteral("BitFlags 位号重复：bit%1").arg(bitIndex));
                continue;
            }
            seenBits.insert(bitIndex);

            const QStringList parts = line.mid(separator + 1).split(QLatin1Char('|'), Qt::KeepEmptyParts);
            const QString name = parts.value(0).trimmed().isEmpty() ? bitLabel(bitIndex) : parts.value(0).trimmed();
            const QString inactiveText = parts.value(1).trimmed().isEmpty() ? QStringLiteral("未置位") : parts.value(1).trimmed();
            const QString activeText = parts.value(2).trimmed().isEmpty() ? QStringLiteral("置位") : parts.value(2).trimmed();
            previewParts.append(QStringLiteral("bit%1 %2（0=%3，1=%4）").arg(bitIndex).arg(name, inactiveText, activeText));
        }

        result.definitionCount = seenBits.size();
        result.valid = result.errors.isEmpty();
        result.previewText = previewParts.isEmpty()
            ? QStringLiteral("未识别到有效 BitFlags 位定义。")
            : QStringLiteral("已识别 %1 个位定义：%2").arg(previewParts.size()).arg(previewHead(previewParts).join(QStringLiteral("；")));
        return result;
    }

    if (candidateType == QStringLiteral("EnumMap")) {
        QSet<int> seenValues;
        QStringList previewParts;
        for (const QString& line : interpretationMapLines(trimmedText)) {
            const qsizetype separator = line.indexOf(QLatin1Char('='));
            if (separator <= 0) {
                result.errors.append(QStringLiteral("EnumMap 映射缺少等号：%1").arg(line));
                continue;
            }

            int value = 0;
            if (!parseLineKey(line.left(separator), &value)) {
                result.errors.append(QStringLiteral("EnumMap 枚举值必须是整数：%1").arg(line));
                continue;
            }
            if (seenValues.contains(value)) {
                result.errors.append(QStringLiteral("EnumMap 枚举值重复：%1").arg(value));
                continue;
            }
            seenValues.insert(value);

            const QString label = line.mid(separator + 1).trimmed();
            if (label.isEmpty()) {
                result.errors.append(QStringLiteral("EnumMap 枚举含义不能为空：%1").arg(line));
                continue;
            }
            previewParts.append(QStringLiteral("%1=%2").arg(value).arg(label));
        }

        result.definitionCount = previewParts.size();
        result.valid = result.errors.isEmpty();
        result.previewText = previewParts.isEmpty()
            ? QStringLiteral("未识别到有效 EnumMap 枚举定义。")
            : QStringLiteral("已识别 %1 个枚举值：%2").arg(previewParts.size()).arg(previewHead(previewParts).join(QStringLiteral("；")));
        return result;
    }

    result.previewText = QStringLiteral("当前规则类型不会使用解释映射；保存后解释文本不会参与验证。请切换到 BitFlags 或 EnumMap 后再填写。或清空解释映射。 ");
    return result;
}

} // namespace svm::matching
