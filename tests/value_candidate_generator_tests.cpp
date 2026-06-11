#include "matching/value_candidate_generator.h"

#include <limits>

#include <QtTest/QtTest>

using namespace svm::matching;

namespace {

RegisterSample sample(int address, quint16 value, int slaveId = 1, int functionCode = 3)
{
    RegisterSample result;
    result.observationId = address + 1000;
    result.sessionId = QStringLiteral("scan-session-1");
    result.slaveId = slaveId;
    result.functionCode = functionCode;
    result.address = address;
    result.value = value;
    result.blockIndex = 2;
    result.attemptIndex = 1;
    result.observedAtUtc = QDateTime::currentDateTimeUtc();
    return result;
}

TargetValue target(double value)
{
    TargetValue result;
    result.label = QStringLiteral("目标值");
    result.value = value;
    result.unit = QStringLiteral("unit");
    result.sampledAtUtc = QDateTime::currentDateTimeUtc();
    return result;
}

CandidateGenerationOptions onlyUInt16()
{
    CandidateGenerationOptions options;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;
    return options;
}

} // namespace

class ValueCandidateGeneratorTests : public QObject {
    Q_OBJECT

private slots:
    void findsScaledUInt16CandidateWithinTolerance();
    void supportsSingleRegisterByteSwap();
    void findsSignedInt16Candidate();
    void findsUInt32CandidateForBothWordOrders();
    void findsFloat32CandidateFromRegisterPair();
    void findsSingleRegisterPackedBcdCandidate();
    void findsTwoRegisterPackedBcdCandidateWithWordSwap();
    void ignoresInvalidPackedBcdNibbles();
    void findsGray16Candidate();
    void findsGray16CandidateWithByteSwap();
    void findsBitFlagsCandidateWithByteSwap();
    void ignoresNonAdjacentRegisterPairs();
    void rejectsInvalidOptionsWithChineseMessage();
    void sortsBestScoreFirstAndLimitsCount();
    void boundedTopNReplacesEarlierLowerRankedCandidate();
    void boundedTopNPreservesAddressTieBreakOrder();
    void preservesEvidenceBackToScanObservation();
};

void ValueCandidateGeneratorTests::findsScaledUInt16CandidateWithinTolerance()
{
    QList<RegisterSample> samples;
    samples.append(sample(100, 1234));

    CandidateGenerationOptions options = onlyUInt16();
    options.scaleTransforms = {{1.0, 0.0}, {0.01, 0.0}};
    options.tolerance.absolute = 0.02;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(12.35), options);

    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.type, NumericCandidateType::UInt16);
    QCOMPARE(candidate.startAddress, 100);
    QCOMPARE(candidate.registerCount, 1);
    QCOMPARE(candidate.decodedValue, 1234.0);
    QCOMPARE(candidate.scale.multiplier, 0.01);
    QCOMPARE(candidate.engineeringValue, 12.34);
    QVERIFY(candidate.absoluteError > 0.009 && candidate.absoluteError < 0.011);
    QVERIFY(candidate.score > 40.0 && candidate.score < 100.0);
    QVERIFY(candidate.evidenceText.contains(QStringLiteral("单样本候选")));
}

void ValueCandidateGeneratorTests::supportsSingleRegisterByteSwap()
{
    QList<RegisterSample> samples;
    samples.append(sample(10, 0x1234));

    CandidateGenerationOptions options = onlyUInt16();

    const CandidateGenerationResult result = generateValueCandidates(samples, target(0x3412), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().byteOrder, ByteOrder::LittleEndian);
    QCOMPARE(result.candidates.first().decodedValue, 0x3412);
    QVERIFY(result.candidates.first().evidenceText.contains(QStringLiteral("字节交换")));
}

void ValueCandidateGeneratorTests::findsSignedInt16Candidate()
{
    QList<RegisterSample> samples;
    samples.append(sample(20, 0xFF9C));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(-100.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().type, NumericCandidateType::Int16);
    QCOMPARE(result.candidates.first().decodedValue, -100.0);
    QVERIFY(result.candidates.first().score > 90.0);
    QVERIFY(result.candidates.first().score < 100.0);
}

void ValueCandidateGeneratorTests::findsUInt32CandidateForBothWordOrders()
{
    QList<RegisterSample> samples;
    samples.append(sample(30, 0x0001));
    samples.append(sample(31, 0x0002));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    CandidateGenerationResult result = generateValueCandidates(samples, target(65538.0), options);
    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().type, NumericCandidateType::UInt32);
    QCOMPARE(result.candidates.first().wordOrder, WordOrder::HighWordFirst);
    QCOMPARE(result.candidates.first().byteOrder, ByteOrder::BigEndian);

    result = generateValueCandidates(samples, target(131073.0), options);
    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().type, NumericCandidateType::UInt32);
    QCOMPARE(result.candidates.first().wordOrder, WordOrder::LowWordFirst);
    QCOMPARE(result.candidates.first().byteOrder, ByteOrder::BigEndian);
}

void ValueCandidateGeneratorTests::findsFloat32CandidateFromRegisterPair()
{
    QList<RegisterSample> samples;
    samples.append(sample(40, 0x4145, 1, 4));
    samples.append(sample(41, 0x70A4, 1, 4));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = true;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;
    options.tolerance.absolute = 0.001;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(12.34), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.type, NumericCandidateType::Float32);
    QCOMPARE(candidate.wordOrder, WordOrder::HighWordFirst);
    QCOMPARE(candidate.byteOrder, ByteOrder::BigEndian);
    QVERIFY(candidate.absoluteError < 0.001);
}

void ValueCandidateGeneratorTests::findsSingleRegisterPackedBcdCandidate()
{
    QList<RegisterSample> samples;
    samples.append(sample(45, 0x1234));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = true;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(1234.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.type, NumericCandidateType::PackedBCD);
    QCOMPARE(candidate.startAddress, 45);
    QCOMPARE(candidate.registerCount, 1);
    QCOMPARE(candidate.byteOrder, ByteOrder::BigEndian);
    QCOMPARE(candidate.decodedValue, 1234.0);
    QVERIFY(candidate.evidenceText.contains(QStringLiteral("PackedBCD")));
}

void ValueCandidateGeneratorTests::findsTwoRegisterPackedBcdCandidateWithWordSwap()
{
    QList<RegisterSample> samples;
    samples.append(sample(46, 0x5678));
    samples.append(sample(47, 0x1234));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = true;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(12345678.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.type, NumericCandidateType::PackedBCD);
    QCOMPARE(candidate.registerCount, 2);
    QCOMPARE(candidate.wordOrder, WordOrder::LowWordFirst);
    QCOMPARE(candidate.byteOrder, ByteOrder::BigEndian);
    QCOMPARE(candidate.rawRegisters, QList<quint16>({0x5678, 0x1234}));
    QCOMPARE(candidate.decodedValue, 12345678.0);
}

void ValueCandidateGeneratorTests::ignoresInvalidPackedBcdNibbles()
{
    QList<RegisterSample> samples;
    samples.append(sample(48, 0x12A4));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = true;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(124.0), options);

    QVERIFY(result.success);
    QVERIFY(result.candidates.isEmpty());
}

void ValueCandidateGeneratorTests::findsGray16Candidate()
{
    QList<RegisterSample> samples;
    samples.append(sample(49, 0x0056)); // binary 100 encoded as Gray16: 100 ^ (100 >> 1) = 0x0056

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = true;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(100.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.type, NumericCandidateType::Gray16);
    QCOMPARE(candidate.startAddress, 49);
    QCOMPARE(candidate.registerCount, 1);
    QCOMPARE(candidate.byteOrder, ByteOrder::BigEndian);
    QCOMPARE(candidate.decodedValue, 100.0);
    QVERIFY(candidate.evidenceText.contains(QStringLiteral("Gray16")));
}

void ValueCandidateGeneratorTests::findsGray16CandidateWithByteSwap()
{
    QList<RegisterSample> samples;
    samples.append(sample(50, 0x5600));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = true;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(100.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().type, NumericCandidateType::Gray16);
    QCOMPARE(result.candidates.first().byteOrder, ByteOrder::LittleEndian);
    QCOMPARE(result.candidates.first().decodedValue, 100.0);
}

void ValueCandidateGeneratorTests::findsBitFlagsCandidateWithByteSwap()
{
    QList<RegisterSample> samples;
    samples.append(sample(51, 0x5500));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = true;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(0x0055), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.type, NumericCandidateType::BitFlags);
    QCOMPARE(candidate.startAddress, 51);
    QCOMPARE(candidate.registerCount, 1);
    QCOMPARE(candidate.byteOrder, ByteOrder::LittleEndian);
    QCOMPARE(candidate.decodedValue, 85.0);
    QVERIFY(candidate.evidenceText.contains(QStringLiteral("BitFlags")));
}

void ValueCandidateGeneratorTests::ignoresNonAdjacentRegisterPairs()
{
    QList<RegisterSample> samples;
    samples.append(sample(50, 0x0001));
    samples.append(sample(52, 0x0002));

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = true;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(65538.0), options);

    QVERIFY(result.success);
    QVERIFY(result.candidates.isEmpty());
}

void ValueCandidateGeneratorTests::rejectsInvalidOptionsWithChineseMessage()
{
    QList<RegisterSample> samples;
    samples.append(sample(1, 1));

    CandidateGenerationOptions options;
    options.scaleTransforms = {{0.0, 0.0}, {std::numeric_limits<double>::quiet_NaN(), 0.0}};

    CandidateGenerationResult result = generateValueCandidates(samples, target(1.0), options);
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("倍率")));

    options = CandidateGenerationOptions{};
    options.tolerance.absolute = -0.1;
    result = generateValueCandidates(samples, target(1.0), options);
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("容差")));

    result = generateValueCandidates({}, target(1.0), CandidateGenerationOptions{});
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("寄存器观测")));
}

void ValueCandidateGeneratorTests::sortsBestScoreFirstAndLimitsCount()
{
    QList<RegisterSample> samples;
    samples.append(sample(1, 100));
    samples.append(sample(2, 101));
    samples.append(sample(3, 102));

    CandidateGenerationOptions options = onlyUInt16();
    options.tolerance.absolute = 2.0;
    options.maxCandidates = 2;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(101.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 2);
    QCOMPARE(result.candidates.at(0).startAddress, 2);
    QCOMPARE(result.candidates.at(0).score, 100.0);
    QVERIFY(result.candidates.at(1).score < result.candidates.at(0).score);
}

void ValueCandidateGeneratorTests::boundedTopNReplacesEarlierLowerRankedCandidate()
{
    QList<RegisterSample> samples;
    samples.append(sample(1, 100));

    CandidateGenerationOptions options = onlyUInt16();
    options.scaleTransforms = {{1.0, 5.0}, {1.0, 0.0}};
    options.tolerance.absolute = 10.0;
    options.maxCandidates = 1;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(100.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    QCOMPARE(result.candidates.first().startAddress, 1);
    QCOMPARE(result.candidates.first().scale.offset, 0.0);
    QCOMPARE(result.candidates.first().absoluteError, 0.0);
    QCOMPARE(result.candidates.first().score, 100.0);
}

void ValueCandidateGeneratorTests::boundedTopNPreservesAddressTieBreakOrder()
{
    QList<RegisterSample> samples;
    samples.append(sample(3, 100));
    samples.append(sample(1, 100));
    samples.append(sample(4, 100));
    samples.append(sample(2, 100));

    CandidateGenerationOptions options = onlyUInt16();
    options.maxCandidates = 2;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(100.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 2);
    QCOMPARE(result.candidates.at(0).startAddress, 1);
    QCOMPARE(result.candidates.at(1).startAddress, 2);
}

void ValueCandidateGeneratorTests::preservesEvidenceBackToScanObservation()
{
    QList<RegisterSample> samples;
    RegisterSample first = sample(70, 0x0000);
    first.observationId = 501;
    first.blockIndex = 7;
    first.attemptIndex = 2;
    RegisterSample second = sample(71, 0x0064);
    second.observationId = 502;
    second.blockIndex = 8;
    second.attemptIndex = 3;
    samples.append(first);
    samples.append(second);

    CandidateGenerationOptions options;
    options.includeUInt16 = false;
    options.includeInt16 = false;
    options.includeUInt32 = true;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;

    const CandidateGenerationResult result = generateValueCandidates(samples, target(100.0), options);

    QVERIFY(result.success);
    QCOMPARE(result.candidates.size(), 1);
    const ValueMatchCandidate& candidate = result.candidates.first();
    QCOMPARE(candidate.sessionId, QStringLiteral("scan-session-1"));
    QCOMPARE(candidate.observationIds, QList<qint64>({501, 502}));
    QCOMPARE(candidate.addresses, QList<int>({70, 71}));
    QCOMPARE(candidate.blockIndexes, QList<int>({7, 8}));
    QCOMPARE(candidate.attemptIndexes, QList<int>({2, 3}));
}

QTEST_MAIN(ValueCandidateGeneratorTests)
#include "value_candidate_generator_tests.moc"
