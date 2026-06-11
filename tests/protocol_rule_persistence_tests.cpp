#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "storage/session_store.h"

namespace {

svm::storage::ProtocolFieldRuleRecord makeRule(QString ruleId = QStringLiteral("rule-1"), QString fieldName = QStringLiteral("温度"))
{
    svm::storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = ruleId;
    rule.fieldName = fieldName;
    rule.sourceStabilityRunId = QStringLiteral("stability-run-1");
    rule.sourceStableCandidateId = 42;
    rule.candidateType = QStringLiteral("Float32");
    rule.wordOrder = QStringLiteral("HighWordFirst");
    rule.byteOrder = QStringLiteral("BigEndian");
    rule.slaveId = 1;
    rule.functionCode = 3;
    rule.startAddress = 100;
    rule.registerCount = 2;
    rule.scaleMultiplier = 1.0;
    rule.scaleOffset = 0.0;
    rule.unit = QStringLiteral("℃");
    rule.confidenceLevel = QStringLiteral("高");
    rule.stabilityScore = 96.5;
    rule.evidenceSummary = QStringLiteral("3 次样本，稳定性评分 96.5");
    rule.interpretationMap = QStringLiteral("0=停止\n1=运行");
    rule.createdAtUtc = QDateTime::currentDateTimeUtc();
    return rule;
}


void dropTableForReadFailure(const QString& databasePath, const QString& tableName)
{
    const QString connectionName = QStringLiteral("drop-table-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(databasePath);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        QVERIFY2(query.exec(QStringLiteral("DROP TABLE %1").arg(tableName)), qPrintable(query.lastError().text()));
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

} // namespace

class ProtocolRulePersistenceTests final : public QObject {
    Q_OBJECT

private slots:
    void savesAndReloadsProtocolFieldRule()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rules.sqlite"))), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveProtocolFieldRule(makeRule()), qPrintable(store.lastErrorText()));

        const auto loaded = store.protocolFieldRule(QStringLiteral("rule-1"));
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->ruleId, QStringLiteral("rule-1"));
        QCOMPARE(loaded->fieldName, QStringLiteral("温度"));
        QCOMPARE(loaded->sourceStabilityRunId, QStringLiteral("stability-run-1"));
        QCOMPARE(loaded->sourceStableCandidateId, 42);
        QCOMPARE(loaded->candidateType, QStringLiteral("Float32"));
        QCOMPARE(loaded->wordOrder, QStringLiteral("HighWordFirst"));
        QCOMPARE(loaded->byteOrder, QStringLiteral("BigEndian"));
        QCOMPARE(loaded->slaveId, 1);
        QCOMPARE(loaded->functionCode, 3);
        QCOMPARE(loaded->startAddress, 100);
        QCOMPARE(loaded->registerCount, 2);
        QCOMPARE(loaded->unit, QStringLiteral("℃"));
        QCOMPARE(loaded->confidenceLevel, QStringLiteral("高"));
        QCOMPARE(loaded->stabilityScore, 96.5);
        QCOMPARE(loaded->interpretationMap, QStringLiteral("0=停止\n1=运行"));
        QVERIFY(loaded->createdAtUtc.isValid());

        const auto recent = store.recentProtocolFieldRules();
        QCOMPARE(recent.size(), 1);
        QCOMPARE(recent.first().ruleId, QStringLiteral("rule-1"));
    }

    void updatesRuleWhenSavingSameRuleId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-update.sqlite"))), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveProtocolFieldRule(makeRule(QStringLiteral("same-rule"), QStringLiteral("温度"))), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveProtocolFieldRule(makeRule(QStringLiteral("same-rule"), QStringLiteral("压力"))), qPrintable(store.lastErrorText()));

        const auto loaded = store.protocolFieldRule(QStringLiteral("same-rule"));
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->fieldName, QStringLiteral("压力"));
        QCOMPARE(store.recentProtocolFieldRules().size(), 1);
    }

    void deletesProtocolFieldRule()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-delete.sqlite"))), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveProtocolFieldRule(makeRule(QStringLiteral("delete-rule"), QStringLiteral("温度"))), qPrintable(store.lastErrorText()));
        QVERIFY(store.protocolFieldRule(QStringLiteral("delete-rule")).has_value());

        QVERIFY2(store.deleteProtocolFieldRule(QStringLiteral("delete-rule")), qPrintable(store.lastErrorText()));
        QVERIFY(!store.protocolFieldRule(QStringLiteral("delete-rule")).has_value());
        QVERIFY(store.recentProtocolFieldRules().isEmpty());
    }


    void surfacesProtocolRuleReadFailures()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString databasePath = dir.filePath(QStringLiteral("rule-read-error.sqlite"));

        svm::storage::SessionStore store;
        QVERIFY2(store.open(databasePath), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveProtocolFieldRule(makeRule(QStringLiteral("read-error-rule"), QStringLiteral("温度"))), qPrintable(store.lastErrorText()));

        dropTableForReadFailure(databasePath, QStringLiteral("protocol_field_rules"));

        QVERIFY(store.recentProtocolFieldRules().isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取最近协议字段规则失败")));

        const auto missingRule = store.protocolFieldRule(QStringLiteral("read-error-rule"));
        QVERIFY(!missingRule.has_value());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取协议字段规则失败")));
    }

    void rejectsEmptyRuleIdWhenDeletingWithChineseError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-delete-invalid.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(!store.deleteProtocolFieldRule(QString()));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("规则 ID")));
    }

    void rejectsInvalidRuleWithChineseError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-invalid.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY(!store.saveProtocolFieldRule(makeRule(QString(), QStringLiteral("温度"))));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("规则 ID")));
        QVERIFY(!store.saveProtocolFieldRule(makeRule(QStringLiteral("rule-no-name"), QString())));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("字段名称")));
    }
};

QTEST_MAIN(ProtocolRulePersistenceTests)
#include "protocol_rule_persistence_tests.moc"
