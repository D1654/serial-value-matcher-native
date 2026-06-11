#include <QtTest/QtTest>

#include "matching/protocol_rule_metadata.h"

class ProtocolRuleMetadataTests final : public QObject {
    Q_OBJECT

private slots:
    void exposesSupportedTypesInStableOrder()
    {
        const QStringList names = svm::matching::supportedProtocolRuleTypeNames();
        QCOMPARE(names, QStringList({
            QStringLiteral("UInt16"),
            QStringLiteral("Int16"),
            QStringLiteral("UInt32"),
            QStringLiteral("Int32"),
            QStringLiteral("Float32"),
            QStringLiteral("PackedBCD"),
            QStringLiteral("Gray16"),
            QStringLiteral("BitFlags"),
            QStringLiteral("EnumMap"),
        }));
    }

    void validatesExactRegisterCounts()
    {
        QVERIFY(svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("UInt16"), 1).isEmpty());
        QVERIFY(svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("Float32"), 2).isEmpty());
        QVERIFY(svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("BitFlags"), 1).isEmpty());

        const QString uint32Error = svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("UInt32"), 1);
        QVERIFY(uint32Error.contains(QStringLiteral("UInt32")));
        QVERIFY(uint32Error.contains(QStringLiteral("2 个寄存器")));
        QVERIFY(uint32Error.contains(QStringLiteral("当前规则为 1 个寄存器")));

        const QString enumError = svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("EnumMap"), 2);
        QVERIFY(enumError.contains(QStringLiteral("EnumMap")));
        QVERIFY(enumError.contains(QStringLiteral("1 个寄存器")));
    }

    void validatesPackedBcdRange()
    {
        QVERIFY(svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("PackedBCD"), 1).isEmpty());
        QVERIFY(svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("PackedBCD"), 2).isEmpty());

        const QString error = svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("PackedBCD"), 3);
        QVERIFY(error.contains(QStringLiteral("PackedBCD")));
        QVERIFY(error.contains(QStringLiteral("1-2 个寄存器")));
    }

    void reportsUnsupportedType()
    {
        const QString error = svm::matching::validateProtocolRuleTypeAndRegisterCount(QStringLiteral("CustomJson"), 1);
        QVERIFY(error.contains(QStringLiteral("暂不支持")));
        QVERIFY(error.contains(QStringLiteral("CustomJson")));
    }

    void exposesInterpretationMapSupport()
    {
        QVERIFY(svm::matching::protocolRuleTypeSupportsInterpretationMap(QStringLiteral("BitFlags")));
        QVERIFY(svm::matching::protocolRuleTypeSupportsInterpretationMap(QStringLiteral("EnumMap")));
        QVERIFY(!svm::matching::protocolRuleTypeSupportsInterpretationMap(QStringLiteral("UInt16")));
        QVERIFY(!svm::matching::protocolRuleTypeSupportsInterpretationMap(QStringLiteral("CustomJson")));
    }
};

QTEST_MAIN(ProtocolRuleMetadataTests)
#include "protocol_rule_metadata_tests.moc"
