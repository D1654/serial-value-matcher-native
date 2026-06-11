#include <QtTest/QtTest>

#include "matching/protocol_rule_verifier.h"

namespace {

svm::storage::ProtocolFieldRuleRecord makeRule(QString ruleId = QStringLiteral("rule-1"),
                                               QString candidateType = QStringLiteral("UInt16"),
                                               int startAddress = 10,
                                               int registerCount = 1)
{
    svm::storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = ruleId;
    rule.fieldName = QStringLiteral("温度");
    rule.candidateType = candidateType;
    rule.wordOrder = QStringLiteral("HighWordFirst");
    rule.byteOrder = QStringLiteral("BigEndian");
    rule.slaveId = 1;
    rule.functionCode = 3;
    rule.startAddress = startAddress;
    rule.registerCount = registerCount;
    rule.scaleMultiplier = 1.0;
    rule.scaleOffset = 0.0;
    rule.unit = QStringLiteral("℃");
    rule.confidenceLevel = QStringLiteral("高");
    rule.stabilityScore = 96.0;
    rule.evidenceSummary = QStringLiteral("测试规则");
    rule.createdAtUtc = QDateTime::currentDateTimeUtc();
    return rule;
}

svm::matching::RegisterSample makeSample(int address, quint16 value, qint64 observationId = 1)
{
    svm::matching::RegisterSample sample;
    sample.observationId = observationId;
    sample.sessionId = QStringLiteral("scan-session-1");
    sample.slaveId = 1;
    sample.functionCode = 3;
    sample.address = address;
    sample.value = value;
    sample.blockIndex = 0;
    sample.attemptIndex = 0;
    sample.observedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-04T10:00:00.000Z"), Qt::ISODateWithMs).addMSecs(observationId);
    return sample;
}

} // namespace

class ProtocolRuleVerifierTests final : public QObject {
    Q_OBJECT

private slots:
    void verifiesUInt16RuleWithScaleUsingNewestObservation()
    {
        auto rule = makeRule();
        rule.scaleMultiplier = 0.1;

        QList<svm::matching::RegisterSample> samples;
        samples.append(makeSample(10, 100, 1));
        samples.append(makeSample(10, 250, 2));

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, samples);
        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QCOMPARE(summary.missingRules, 0);
        QCOMPARE(summary.unsupportedRules, 0);
        QCOMPARE(summary.results.size(), 1);
        QVERIFY(summary.results.first().verified);
        QCOMPARE(summary.results.first().candidateType, QStringLiteral("UInt16"));
        QCOMPARE(summary.results.first().decodedValue, 250.0);
        QCOMPARE(summary.results.first().engineeringValue, 25.0);
        QCOMPARE(summary.results.first().observationIds, QList<qint64>{2});
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("工程值 25℃")));
    }

    void indexesNewestSamplesWithExistingTieBreakers()
    {
        auto rule = makeRule();
        const QDateTime sameTimestamp = QDateTime::fromString(QStringLiteral("2026-06-04T10:00:00.000Z"), Qt::ISODateWithMs);

        auto olderBlock = makeSample(10, 100, 10);
        olderBlock.observedAtUtc = sameTimestamp;
        olderBlock.blockIndex = 1;
        olderBlock.attemptIndex = 9;

        auto newerBlock = makeSample(10, 200, 20);
        newerBlock.observedAtUtc = sameTimestamp;
        newerBlock.blockIndex = 2;
        newerBlock.attemptIndex = 1;

        auto newerAttempt = makeSample(10, 300, 30);
        newerAttempt.observedAtUtc = sameTimestamp;
        newerAttempt.blockIndex = 2;
        newerAttempt.attemptIndex = 2;

        auto newestObservation = makeSample(10, 400, 40);
        newestObservation.observedAtUtc = sameTimestamp;
        newestObservation.blockIndex = 2;
        newestObservation.attemptIndex = 2;

        auto differentFunctionCode = makeSample(10, 999, 99);
        differentFunctionCode.functionCode = 4;
        differentFunctionCode.observedAtUtc = sameTimestamp.addSecs(60);

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            newerAttempt,
            differentFunctionCode,
            olderBlock,
            newestObservation,
            newerBlock,
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QCOMPARE(summary.results.first().decodedValue, 400.0);
        QCOMPARE(summary.results.first().observationIds, QList<qint64>{40});
        QCOMPARE(summary.results.first().rawRegisters, QList<quint16>{400});
    }

    void verifiesFloat32Rule()
    {
        auto rule = makeRule(QStringLiteral("float-rule"), QStringLiteral("Float32"), 100, 2);

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(100, 0x4148, 10),
            makeSample(101, 0x0000, 11),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QVERIFY(summary.results.first().verified);
        QCOMPARE(summary.results.first().rawRegisters, QList<quint16>({0x4148, 0x0000}));
        QVERIFY(qAbs(summary.results.first().engineeringValue - 12.5) < 0.000001);
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("Float32")));
    }

    void verifiesPackedBcdRuleWithScaleAndByteOrder()
    {
        auto rule = makeRule(QStringLiteral("bcd-rule"), QStringLiteral("PackedBCD"), 40, 1);
        rule.byteOrder = QStringLiteral("LittleEndian");
        rule.scaleMultiplier = 0.1;

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(40, 0x3412, 40),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QCOMPARE(summary.unsupportedRules, 0);
        QVERIFY(summary.results.first().verified);
        QCOMPARE(summary.results.first().decodedValue, 1234.0);
        QVERIFY(qAbs(summary.results.first().engineeringValue - 123.4) < 0.000001);
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("PackedBCD")));
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("工程值 123.4℃")));
    }

    void rejectsPackedBcdRuleWithInvalidDigit()
    {
        auto rule = makeRule(QStringLiteral("bad-bcd-rule"), QStringLiteral("PackedBCD"), 41, 1);

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(41, 0x12A4, 41),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 0);
        QCOMPARE(summary.unsupportedRules, 1);
        QVERIFY(!summary.results.first().verified);
        QVERIFY(summary.results.first().statusText.contains(QStringLiteral("解码失败")));
    }

    void verifiesGray16RuleWithByteOrder()
    {
        auto rule = makeRule(QStringLiteral("gray-rule"), QStringLiteral("Gray16"), 42, 1);
        rule.byteOrder = QStringLiteral("LittleEndian");

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(42, 0x5600, 42),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QCOMPARE(summary.unsupportedRules, 0);
        QVERIFY(summary.results.first().verified);
        QCOMPARE(summary.results.first().decodedValue, 100.0);
        QCOMPARE(summary.results.first().engineeringValue, 100.0);
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("Gray16")));
    }

    void verifiesBitFlagsRuleWithByteOrder()
    {
        auto rule = makeRule(QStringLiteral("flags-rule"), QStringLiteral("BitFlags"), 43, 1);
        rule.fieldName = QStringLiteral("状态字");
        rule.byteOrder = QStringLiteral("LittleEndian");
        rule.unit.clear();
        rule.interpretationMap = QStringLiteral("0=运行允许|未允许|已允许\n1=报警|正常|报警触发\n2=远程模式|本地|远程");

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(43, 0x5500, 43),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QCOMPARE(summary.unsupportedRules, 0);
        QVERIFY(summary.results.first().verified);
        QCOMPARE(summary.results.first().decodedValue, 85.0);
        QCOMPARE(summary.results.first().engineeringValue, 85.0);
        QCOMPARE(summary.results.first().rawRegisters, QList<quint16>{0x5500});
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("BitFlags")));
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("原始寄存器 0x5500")));
        QVERIFY(summary.results.first().interpretationText.contains(QStringLiteral("bit0 运行允许=已允许")));
        QVERIFY(summary.results.first().interpretationText.contains(QStringLiteral("bit1 报警=正常")));
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("位解释")));
    }

    void verifiesEnumMapRuleWithChineseInterpretation()
    {
        auto rule = makeRule(QStringLiteral("enum-rule"), QStringLiteral("EnumMap"), 44, 1);
        rule.fieldName = QStringLiteral("运行状态");
        rule.unit.clear();
        rule.interpretationMap = QStringLiteral("0=停止\n1=运行\n2=故障");

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(44, 0x0002, 44),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 1);
        QCOMPARE(summary.unsupportedRules, 0);
        QVERIFY(summary.results.first().verified);
        QCOMPARE(summary.results.first().decodedValue, 2.0);
        QCOMPARE(summary.results.first().engineeringValue, 2.0);
        QVERIFY(summary.results.first().interpretationText.contains(QStringLiteral("枚举解释：故障")));
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("EnumMap")));
        QVERIFY(summary.results.first().evidenceText.contains(QStringLiteral("枚举解释")));
    }

    void reportsMissingObservationWithChineseStatus()
    {
        auto rule = makeRule(QStringLiteral("missing-rule"), QStringLiteral("UInt32"), 20, 2);

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(20, 0x0001, 20),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 0);
        QCOMPARE(summary.missingRules, 1);
        QVERIFY(!summary.results.first().verified);
        QVERIFY(summary.results.first().statusText.contains(QStringLiteral("缺少地址 21")));
    }

    void rejectsRegisterCountMismatchBeforeMatchingSamples()
    {
        auto rule = makeRule(QStringLiteral("bad-count-rule"), QStringLiteral("Float32"), 50, 1);

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(50, 0x4148, 50),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 0);
        QCOMPARE(summary.missingRules, 0);
        QCOMPARE(summary.unsupportedRules, 1);
        QVERIFY(!summary.results.first().verified);
        QVERIFY(summary.results.first().statusText.contains(QStringLiteral("Float32")));
        QVERIFY(summary.results.first().statusText.contains(QStringLiteral("2 个寄存器")));
        QVERIFY(summary.results.first().statusText.contains(QStringLiteral("当前规则为 1 个寄存器")));
    }

    void reportsUnsupportedTypeWithChineseStatus()
    {
        auto rule = makeRule(QStringLiteral("custom-rule"), QStringLiteral("CustomJson"), 30, 1);

        const auto summary = svm::matching::verifyProtocolFieldRules({rule}, {
            makeSample(30, 0x1234, 30),
        });

        QCOMPARE(summary.totalRules, 1);
        QCOMPARE(summary.verifiedRules, 0);
        QCOMPARE(summary.unsupportedRules, 1);
        QVERIFY(summary.results.first().statusText.contains(QStringLiteral("暂不支持")));
    }
};

QTEST_MAIN(ProtocolRuleVerifierTests)
#include "protocol_rule_verifier_tests.moc"
