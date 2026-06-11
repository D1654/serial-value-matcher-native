#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace svm::matching {

struct BitFlagInterpretationDefinition {
    int bitIndex = -1;
    QString name;
    QString inactiveText;
    QString activeText;
};

struct EnumMapInterpretationDefinition {
    int value = 0;
    QString label;
};

struct InterpretationMapValidationResult {
    bool valid = true;
    int definitionCount = 0;
    QStringList errors;
    QString previewText;
};

QStringList interpretationMapLines(QString text);
QList<BitFlagInterpretationDefinition> parseBitFlagInterpretationDefinitions(const QString& text);
QList<EnumMapInterpretationDefinition> parseEnumMapInterpretationDefinitions(const QString& text);
QString bitFlagInterpretationText(const QString& text, quint16 mask);
QString enumMapInterpretationText(const QString& text, int value);
InterpretationMapValidationResult validateInterpretationMap(const QString& candidateType, const QString& text);

} // namespace svm::matching
