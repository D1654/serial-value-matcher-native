#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "storage/session_store.h"

class SendHistoryTests final : public QObject {
    Q_OBJECT

private slots:
    void savesAndListsRecentHistory() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("history.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(store.saveSendHistory(QStringLiteral("01 03"), svm::protocol::PayloadMode::Hex, svm::protocol::LineEnding::CrLf));
        QVERIFY(store.saveSendHistory(QStringLiteral("hello"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None));

        const auto entries = store.recentSendHistory(10);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().content, QStringLiteral("hello"));
        QCOMPARE(entries.first().payloadMode, svm::protocol::PayloadMode::Text);
    }

    void duplicateMovesToLatest() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("history.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(store.saveSendHistory(QStringLiteral("A"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None));
        QVERIFY(store.saveSendHistory(QStringLiteral("B"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None));
        QVERIFY(store.saveSendHistory(QStringLiteral("A"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None));

        const auto entries = store.recentSendHistory(10);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().content, QStringLiteral("A"));
        QCOMPARE(entries.last().content, QStringLiteral("B"));
    }

    void trimsByLimit() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("history.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(store.saveSendHistory(QStringLiteral("A"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None, 2));
        QVERIFY(store.saveSendHistory(QStringLiteral("B"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None, 2));
        QVERIFY(store.saveSendHistory(QStringLiteral("C"), svm::protocol::PayloadMode::Text, svm::protocol::LineEnding::None, 2));

        const auto entries = store.recentSendHistory(10);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().content, QStringLiteral("C"));
        QCOMPARE(entries.last().content, QStringLiteral("B"));
    }
};

QTEST_MAIN(SendHistoryTests)
#include "send_history_tests.moc"
