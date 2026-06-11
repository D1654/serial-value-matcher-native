#include <QtTest/QtTest>

#include "protocol/payload_codec.h"

class PayloadCodecTests final : public QObject {
    Q_OBJECT

private slots:
    void encodesTextAsUtf8() {
        const auto result = svm::protocol::PayloadCodec::encode(
            QStringLiteral("测试A"),
            svm::protocol::PayloadMode::Text,
            svm::protocol::LineEnding::None);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.payload, QStringLiteral("测试A").toUtf8());
    }

    void encodesHexWithSpaces() {
        const auto result = svm::protocol::PayloadCodec::encode(
            QStringLiteral("01 03 00 00"),
            svm::protocol::PayloadMode::Hex,
            svm::protocol::LineEnding::None);
        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.payload, QByteArray::fromHex("01030000"));
    }

    void rejectsOddHexLength() {
        const auto result = svm::protocol::PayloadCodec::encode(
            QStringLiteral("01 0"),
            svm::protocol::PayloadMode::Hex,
            svm::protocol::LineEnding::None);
        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("偶数")));
    }

    void rejectsInvalidHexCharacter() {
        const auto result = svm::protocol::PayloadCodec::encode(
            QStringLiteral("01 GG"),
            svm::protocol::PayloadMode::Hex,
            svm::protocol::LineEnding::None);
        QVERIFY(!result.ok);
        QVERIFY(result.errorMessage.contains(QStringLiteral("HEX")));
    }

    void appendsLineEndings() {
        QCOMPARE(svm::protocol::PayloadCodec::encode(QStringLiteral("A"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::Cr).payload,
                 QByteArray("A\r", 2));
        QCOMPARE(svm::protocol::PayloadCodec::encode(QStringLiteral("A"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::Lf).payload,
                 QByteArray("A\n", 2));
        QCOMPARE(svm::protocol::PayloadCodec::encode(QStringLiteral("A"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::CrLf).payload,
                 QByteArray("A\r\n", 3));
    }
};

QTEST_MAIN(PayloadCodecTests)
#include "payload_codec_tests.moc"
