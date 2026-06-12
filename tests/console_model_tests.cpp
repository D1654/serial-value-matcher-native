#include <QtTest/QtTest>

#include "session/console_model.h"

class ConsoleModelTests final : public QObject {
    Q_OBJECT

private slots:
    void formatsHexAndEscapedText() {
        svm::capture::RawIoEvent event;
        event.sessionId = QStringLiteral("s1");
        event.direction = svm::capture::Direction::Tx;
        event.timestampUtc = QDateTime::fromString(QStringLiteral("2026-06-01T00:00:00.000Z"), Qt::ISODateWithMs);
        event.endpoint = QStringLiteral("COM1");
        event.payload = QByteArray("A\r\n", 3);

        const auto line = svm::session::ConsoleModel::makeLine(event);
        QCOMPARE(line.directionText, QStringLiteral("TX"));
        QCOMPARE(line.hexText, QStringLiteral("41 0D 0A"));
        QCOMPARE(line.textPreview, QStringLiteral("A\\r\\n"));
        QVERIFY(line.displayLine.contains(QStringLiteral("COM1")));
    }

    void appendStoresLine() {
        svm::session::ConsoleModel model;
        svm::capture::RawIoEvent event;
        event.endpoint = QStringLiteral("COM2");
        event.payload = QByteArray::fromHex("ff");
        model.appendEvent(event);
        QCOMPARE(model.lines().size(), 1);
        QCOMPARE(model.lines().first().hexText, QStringLiteral("FF"));
    }

    void clearRemovesLines() {
        svm::session::ConsoleModel model;
        svm::capture::RawIoEvent event;
        event.endpoint = QStringLiteral("COM3");
        event.payload = QByteArray::fromHex("aa");
        model.appendEvent(event);
        QCOMPARE(model.lines().size(), 1);
        model.clear();
        QCOMPARE(model.lines().size(), 0);
    }

    void maximumLineCountKeepsNewestLines() {
        svm::session::ConsoleModel model;
        model.setMaximumLineCount(2);

        svm::capture::RawIoEvent first;
        first.endpoint = QStringLiteral("COM1");
        first.payload = QByteArray::fromHex("01");
        svm::capture::RawIoEvent second = first;
        second.payload = QByteArray::fromHex("02");
        svm::capture::RawIoEvent third = first;
        third.payload = QByteArray::fromHex("03");

        model.appendEvent(first);
        model.appendEvent(second);
        model.appendEvent(third);

        QCOMPARE(model.lines().size(), 2);
        QCOMPARE(model.lines().first().hexText, QStringLiteral("02"));
        QCOMPARE(model.lines().last().hexText, QStringLiteral("03"));
    }

};

QTEST_MAIN(ConsoleModelTests)
#include "console_model_tests.moc"
