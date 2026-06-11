#include "matching/numeric_decoder.h"

#include <QtTest/QtTest>

using namespace svm::matching;

class NumericDecoderTests final : public QObject {
    Q_OBJECT

private slots:
    void decodesSingleRegisterTypes()
    {
        const auto uint16Value = decodeNumericValue(NumericCandidateType::UInt16,
                                                    WordOrder::HighWordFirst,
                                                    ByteOrder::LittleEndian,
                                                    {0x1234});
        QVERIFY(uint16Value.has_value());
        QCOMPARE(*uint16Value, 0x3412);

        const auto int16Value = decodeNumericValue(NumericCandidateType::Int16,
                                                   WordOrder::HighWordFirst,
                                                   ByteOrder::BigEndian,
                                                   {0xFF9C});
        QVERIFY(int16Value.has_value());
        QCOMPARE(*int16Value, -100.0);

        const auto bitFlagsValue = decodeNumericValue(NumericCandidateType::BitFlags,
                                                      WordOrder::HighWordFirst,
                                                      ByteOrder::LittleEndian,
                                                      {0x5500});
        QVERIFY(bitFlagsValue.has_value());
        QCOMPARE(*bitFlagsValue, 85.0);
    }

    void decodesThirtyTwoBitTypesWithWordAndByteOrder()
    {
        QCOMPARE(combineWords(0x0001, 0x0002, WordOrder::HighWordFirst, ByteOrder::BigEndian), 0x00010002u);
        QCOMPARE(combineWords(0x0001, 0x0002, WordOrder::LowWordFirst, ByteOrder::BigEndian), 0x00020001u);
        QCOMPARE(combineWords(0x3412, 0x7856, WordOrder::HighWordFirst, ByteOrder::LittleEndian), 0x12345678u);

        const auto uint32Value = decodeNumericValue(NumericCandidateType::UInt32,
                                                    WordOrder::HighWordFirst,
                                                    ByteOrder::LittleEndian,
                                                    {0x3412, 0x7856});
        QVERIFY(uint32Value.has_value());
        QCOMPARE(*uint32Value, 305419896.0);

        const auto int32Value = decodeNumericValue(NumericCandidateType::Int32,
                                                   WordOrder::HighWordFirst,
                                                   ByteOrder::BigEndian,
                                                   {0xFFFF, 0xFF9C});
        QVERIFY(int32Value.has_value());
        QCOMPARE(*int32Value, -100.0);

        const auto float32Value = decodeNumericValue(NumericCandidateType::Float32,
                                                     WordOrder::HighWordFirst,
                                                     ByteOrder::BigEndian,
                                                     {0x4148, 0x0000});
        QVERIFY(float32Value.has_value());
        QVERIFY(qAbs(*float32Value - 12.5) < 0.000001);
    }

    void decodesPackedBcdAndRejectsInvalidNibbles()
    {
        const auto singleRegister = decodePackedBcdWords({0x3412},
                                                         WordOrder::HighWordFirst,
                                                         ByteOrder::LittleEndian);
        QVERIFY(singleRegister.has_value());
        QCOMPARE(*singleRegister, 1234.0);

        const auto twoRegister = decodeNumericValue(NumericCandidateType::PackedBCD,
                                                    WordOrder::LowWordFirst,
                                                    ByteOrder::BigEndian,
                                                    {0x5678, 0x1234});
        QVERIFY(twoRegister.has_value());
        QCOMPARE(*twoRegister, 12345678.0);

        const auto invalid = decodePackedBcdWords({0x12A4},
                                                  WordOrder::HighWordFirst,
                                                  ByteOrder::BigEndian);
        QVERIFY(!invalid.has_value());
    }

    void decodesGrayAndProtocolStringKeys()
    {
        QCOMPARE(gray16ToBinary(0x0056), 100);

        const auto grayValue = decodeNumericValue(QStringLiteral("Gray16"),
                                                  QStringLiteral("HighWordFirst"),
                                                  QStringLiteral("LittleEndian"),
                                                  {0x5600});
        QVERIFY(grayValue.has_value());
        QCOMPARE(*grayValue, 100.0);

        const auto enumValue = decodeNumericValue(QStringLiteral("EnumMap"),
                                                  QStringLiteral("HighWordFirst"),
                                                  QStringLiteral("LittleEndian"),
                                                  {0x0200});
        QVERIFY(enumValue.has_value());
        QCOMPARE(*enumValue, 2.0);
    }

    void rejectsUnsupportedTypesAndWrongRegisterCounts()
    {
        QVERIFY(!decodeNumericValue(QStringLiteral("CustomJson"),
                                    QStringLiteral("HighWordFirst"),
                                    QStringLiteral("BigEndian"),
                                    {0x0001}).has_value());
        QVERIFY(!decodeNumericValue(NumericCandidateType::UInt16,
                                    WordOrder::HighWordFirst,
                                    ByteOrder::BigEndian,
                                    {0x0001, 0x0002}).has_value());
        QVERIFY(!decodeNumericValue(NumericCandidateType::Float32,
                                    WordOrder::HighWordFirst,
                                    ByteOrder::BigEndian,
                                    {0x4148}).has_value());
        QVERIFY(!decodeNumericValue(NumericCandidateType::PackedBCD,
                                    WordOrder::HighWordFirst,
                                    ByteOrder::BigEndian,
                                    {}).has_value());
    }
};

QTEST_MAIN(NumericDecoderTests)
#include "numeric_decoder_tests.moc"
