#include <QtTest/QtTest>

#include "matching/protocol_rule_interpretation.h"

class ProtocolRuleInterpretationTests final : public QObject {
    Q_OBJECT

private slots:
    void validatesBitFlagsMapAndBuildsPreview()
    {
        const auto validation = svm::matching::validateInterpretationMap(
            QStringLiteral("BitFlags"),
            QStringLiteral("0=运行允许|未允许|已允许\nbit1=报警|正常|报警触发"));

        QVERIFY(validation.valid);
        QCOMPARE(validation.definitionCount, 2);
        QVERIFY(validation.errors.isEmpty());
        QVERIFY(validation.previewText.contains(QStringLiteral("已识别 2 个位定义")));
        QVERIFY(validation.previewText.contains(QStringLiteral("bit0 运行允许")));
        QVERIFY(validation.previewText.contains(QStringLiteral("1=已允许")));
    }

    void rejectsInvalidBitFlagsMapWithChineseErrors()
    {
        const auto validation = svm::matching::validateInterpretationMap(
            QStringLiteral("BitFlags"),
            QStringLiteral("0=运行允许|未允许|已允许\n16=越界|否|是\n0=重复|否|是\n坏格式"));

        QVERIFY(!validation.valid);
        QVERIFY(validation.errors.join(QStringLiteral("\n")).contains(QStringLiteral("0-15")));
        QVERIFY(validation.errors.join(QStringLiteral("\n")).contains(QStringLiteral("重复")));
        QVERIFY(validation.errors.join(QStringLiteral("\n")).contains(QStringLiteral("缺少等号")));
        QVERIFY(validation.previewText.contains(QStringLiteral("已识别 1 个位定义")));
    }

    void validatesEnumMapAndInterpretsHexValues()
    {
        const QString map = QStringLiteral("0=停止\n1=运行\n0x10=维护");
        const auto validation = svm::matching::validateInterpretationMap(QStringLiteral("EnumMap"), map);

        QVERIFY(validation.valid);
        QCOMPARE(validation.definitionCount, 3);
        QVERIFY(validation.previewText.contains(QStringLiteral("0=停止")));
        QCOMPARE(svm::matching::enumMapInterpretationText(map, 16), QStringLiteral("枚举解释：维护。"));
        QVERIFY(svm::matching::enumMapInterpretationText(map, 2).contains(QStringLiteral("未定义枚举值 2")));
    }

    void rejectsInvalidEnumMapWithChineseErrors()
    {
        const auto validation = svm::matching::validateInterpretationMap(
            QStringLiteral("EnumMap"),
            QStringLiteral("1=运行\n1=重复\n二=中文数字\n3="));

        QVERIFY(!validation.valid);
        const QString errors = validation.errors.join(QStringLiteral("\n"));
        QVERIFY(errors.contains(QStringLiteral("重复")));
        QVERIFY(errors.contains(QStringLiteral("必须是整数")));
        QVERIFY(errors.contains(QStringLiteral("不能为空")));
    }

    void leavesNonInterpretationTypesValidButPreviewed()
    {
        const auto validation = svm::matching::validateInterpretationMap(QStringLiteral("UInt16"), QStringLiteral("1=运行"));

        QVERIFY(validation.valid);
        QVERIFY(validation.previewText.contains(QStringLiteral("当前规则类型不会使用解释映射")));
    }
};

QTEST_MAIN(ProtocolRuleInterpretationTests)
#include "protocol_rule_interpretation_tests.moc"
