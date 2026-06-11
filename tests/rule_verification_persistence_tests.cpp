#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "storage/session_store.h"

namespace {

svm::matching::ProtocolRuleVerificationSummary makeSummary()
{
    svm::matching::ProtocolRuleVerificationSummary summary;
    summary.totalRules = 2;
    summary.verifiedRules = 1;
    summary.missingRules = 1;
    summary.unsupportedRules = 0;

    svm::matching::ProtocolRuleVerificationResult verified;
    verified.ruleId = QStringLiteral("rule-temp");
    verified.fieldName = QStringLiteral("温度");
    verified.unit = QStringLiteral("℃");
    verified.candidateType = QStringLiteral("Float32");
    verified.sourceScanSessionId = QStringLiteral("scan-1");
    verified.verified = true;
    verified.statusText = QStringLiteral("已验证");
    verified.slaveId = 1;
    verified.functionCode = 3;
    verified.startAddress = 100;
    verified.registerCount = 2;
    verified.observationIds = {11, 12};
    verified.rawRegisters = {0x4148, 0x0000};
    verified.decodedValue = 12.5;
    verified.engineeringValue = 12.5;
    verified.observedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-04T10:00:00.000Z"), Qt::ISODateWithMs);
    verified.interpretationText = QStringLiteral("枚举解释：运行。");
    verified.evidenceText = QStringLiteral("字段“温度”验证成功 枚举解释：运行。");
    summary.results.append(verified);

    svm::matching::ProtocolRuleVerificationResult missing;
    missing.ruleId = QStringLiteral("rule-pressure");
    missing.fieldName = QStringLiteral("压力");
    missing.unit = QStringLiteral("kPa");
    missing.candidateType = QStringLiteral("UInt16");
    missing.verified = false;
    missing.statusText = QStringLiteral("缺少地址 200 的观测，无法验证。");
    missing.slaveId = 1;
    missing.functionCode = 3;
    missing.startAddress = 200;
    missing.registerCount = 1;
    missing.evidenceText = missing.statusText;
    summary.results.append(missing);

    return summary;
}

svm::storage::RuleVerificationRunRecord makeRun(QString runId = QStringLiteral("verify-1"))
{
    svm::storage::RuleVerificationRunRecord run;
    run.verificationRunId = runId;
    run.sourceScanSessionId = QStringLiteral("scan-1");
    run.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-06-04T10:01:00.000Z"), Qt::ISODateWithMs);
    return run;
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

class RuleVerificationPersistenceTests final : public QObject {
    Q_OBJECT

private slots:
    void savesAndReloadsRuleVerificationRun()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-verification.sqlite"))), qPrintable(store.lastErrorText()));

        const auto summary = makeSummary();
        QVERIFY2(store.saveRuleVerificationRun(makeRun(), summary), qPrintable(store.lastErrorText()));

        const auto loadedRun = store.ruleVerificationRun(QStringLiteral("verify-1"));
        QVERIFY(loadedRun.has_value());
        QCOMPARE(loadedRun->verificationRunId, QStringLiteral("verify-1"));
        QCOMPARE(loadedRun->sourceScanSessionId, QStringLiteral("scan-1"));
        QCOMPARE(loadedRun->ruleCount, 2);
        QCOMPARE(loadedRun->verifiedCount, 1);
        QCOMPARE(loadedRun->missingCount, 1);
        QCOMPARE(loadedRun->unsupportedCount, 0);
        QVERIFY(loadedRun->createdAtUtc.isValid());

        const auto latest = store.latestRuleVerificationRun();
        QVERIFY(latest.has_value());
        QCOMPARE(latest->verificationRunId, QStringLiteral("verify-1"));

        const auto results = store.ruleVerificationResults(QStringLiteral("verify-1"));
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.at(0).ruleId, QStringLiteral("rule-temp"));
        QCOMPARE(results.at(0).fieldName, QStringLiteral("温度"));
        QCOMPARE(results.at(0).candidateType, QStringLiteral("Float32"));
        QCOMPARE(results.at(0).verified, true);
        QCOMPARE(results.at(0).observationIds, QList<qint64>({11, 12}));
        QCOMPARE(results.at(0).rawRegisters, QList<int>({0x4148, 0x0000}));
        QCOMPARE(results.at(0).decodedValue, 12.5);
        QCOMPARE(results.at(0).engineeringValue, 12.5);
        QCOMPARE(results.at(0).interpretationText, QStringLiteral("枚举解释：运行。"));
        QVERIFY(results.at(0).evidenceText.contains(QStringLiteral("验证成功")));
        QCOMPARE(results.at(1).ruleId, QStringLiteral("rule-pressure"));
        QCOMPARE(results.at(1).candidateType, QStringLiteral("UInt16"));
        QCOMPARE(results.at(1).verified, false);
        QVERIFY(results.at(1).statusText.contains(QStringLiteral("缺少地址")));
    }

    void replacesResultsWhenSavingSameVerificationRunId()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-verification-replace.sqlite"))), qPrintable(store.lastErrorText()));

        QVERIFY2(store.saveRuleVerificationRun(makeRun(QStringLiteral("same-run")), makeSummary()), qPrintable(store.lastErrorText()));

        auto replacement = makeSummary();
        replacement.totalRules = 1;
        replacement.verifiedRules = 1;
        replacement.missingRules = 0;
        replacement.unsupportedRules = 0;
        replacement.results = {replacement.results.first()};
        QVERIFY2(store.saveRuleVerificationRun(makeRun(QStringLiteral("same-run")), replacement), qPrintable(store.lastErrorText()));

        const auto loadedRun = store.ruleVerificationRun(QStringLiteral("same-run"));
        QVERIFY(loadedRun.has_value());
        QCOMPARE(loadedRun->ruleCount, 1);
        QCOMPARE(loadedRun->verifiedCount, 1);
        QCOMPARE(loadedRun->missingCount, 0);
        QCOMPARE(store.ruleVerificationResults(QStringLiteral("same-run")).size(), 1);
    }


    void surfacesRuleVerificationReadFailures()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString databasePath = dir.filePath(QStringLiteral("verification-read-error.sqlite"));

        svm::storage::SessionStore store;
        QVERIFY2(store.open(databasePath), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveRuleVerificationRun(makeRun(QStringLiteral("verify-read-error")), makeSummary()), qPrintable(store.lastErrorText()));

        dropTableForReadFailure(databasePath, QStringLiteral("rule_verification_results"));

        const auto results = store.ruleVerificationResults(QStringLiteral("verify-read-error"));
        QVERIFY(results.isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取规则验证明细失败")));

        dropTableForReadFailure(databasePath, QStringLiteral("rule_verification_runs"));

        const auto run = store.ruleVerificationRun(QStringLiteral("verify-read-error"));
        QVERIFY(!run.has_value());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取规则验证运行失败")));

        const auto latestRun = store.latestRuleVerificationRun();
        QVERIFY(!latestRun.has_value());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取最近规则验证运行失败")));
    }

    void rejectsInvalidRunWithChineseError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dir.filePath(QStringLiteral("rule-verification-invalid.sqlite"))), qPrintable(store.lastErrorText()));

        auto run = makeRun(QString());
        QVERIFY(!store.saveRuleVerificationRun(run, makeSummary()));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("验证运行 ID")));

        run = makeRun(QStringLiteral("verify-no-session"));
        run.sourceScanSessionId.clear();
        QVERIFY(!store.saveRuleVerificationRun(run, makeSummary()));
        QVERIFY(store.lastErrorText().contains(QStringLiteral("来源扫描会话")));
    }
};

QTEST_MAIN(RuleVerificationPersistenceTests)
#include "rule_verification_persistence_tests.moc"
