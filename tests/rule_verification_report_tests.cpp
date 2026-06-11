#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "report/rule_verification_report.h"
#include "report/text_file_writer.h"

namespace {

svm::storage::RuleVerificationRunRecord makeRun()
{
    svm::storage::RuleVerificationRunRecord run;
    run.verificationRunId = QStringLiteral("verify-1");
    run.sourceScanSessionId = QStringLiteral("scan-1");
    run.ruleCount = 2;
    run.verifiedCount = 1;
    run.missingCount = 1;
    run.unsupportedCount = 0;
    run.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-06-04T10:00:00.000Z"), Qt::ISODateWithMs);
    return run;
}

QList<svm::storage::RuleVerificationResultRecord> makeResults()
{
    svm::storage::RuleVerificationResultRecord verified;
    verified.verificationRunId = QStringLiteral("verify-1");
    verified.ruleId = QStringLiteral("rule-temp");
    verified.fieldName = QStringLiteral("温度|出水");
    verified.unit = QStringLiteral("℃");
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
    verified.interpretationText = QStringLiteral("位解释：bit0 运行允许=已允许。");
    verified.evidenceText = QStringLiteral("字段验证成功\n来自扫描观测");

    svm::storage::RuleVerificationResultRecord missing;
    missing.verificationRunId = QStringLiteral("verify-1");
    missing.ruleId = QStringLiteral("rule-pressure");
    missing.fieldName = QStringLiteral("压力");
    missing.unit = QStringLiteral("kPa");
    missing.verified = false;
    missing.statusText = QStringLiteral("缺少地址 200 的观测，无法验证。");
    missing.slaveId = 1;
    missing.functionCode = 3;
    missing.startAddress = 200;
    missing.registerCount = 1;
    missing.evidenceText = missing.statusText;

    return {verified, missing};
}

} // namespace

class RuleVerificationReportTests final : public QObject {
    Q_OBJECT

private slots:
    void rendersChineseMarkdownReport()
    {
        const QString markdown = svm::report::renderRuleVerificationMarkdownReport(makeRun(), makeResults());

        QVERIFY(markdown.startsWith(QStringLiteral("# 协议规则验证报告")));
        QVERIFY(markdown.contains(QStringLiteral("## 验证摘要")));
        QVERIFY(markdown.contains(QStringLiteral("- 验证运行 ID：verify-1")));
        QVERIFY(markdown.contains(QStringLiteral("- 来源扫描会话：scan-1")));
        QVERIFY(markdown.contains(QStringLiteral("- 总规则数：2")));
        QVERIFY(markdown.contains(QStringLiteral("| 结果 | 字段 | 工程值 | 单位 |")));
        QVERIFY(markdown.contains(QStringLiteral("| 解释 | 证据 |")));
        QVERIFY(markdown.contains(QStringLiteral("温度\\|出水")));
        QVERIFY(markdown.contains(QStringLiteral("12.5")));
        QVERIFY(markdown.contains(QStringLiteral("0x4148, 0x0000")));
        QVERIFY(markdown.contains(QStringLiteral("11, 12")));
        QVERIFY(markdown.contains(QStringLiteral("位解释：bit0 运行允许=已允许。")));
        QVERIFY(markdown.contains(QStringLiteral("字段验证成功<br>来自扫描观测")));
        QVERIFY(markdown.contains(QStringLiteral("缺少地址 200")));
        QVERIFY(markdown.contains(QStringLiteral("不会修改原始扫描")));
    }

    void rendersEmptyDetailState()
    {
        const QString markdown = svm::report::renderRuleVerificationMarkdownReport(makeRun(), {});
        QVERIFY(markdown.contains(QStringLiteral("暂无验证明细。")));
    }

    void writesUtf8ReportTextExactly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("report.md"));
        const QString text = QStringLiteral("# 报告\n温度：12.5℃\n");

        const svm::report::TextFileWriteResult result = svm::report::writeUtf8TextFile(filePath, text);
        QVERIFY2(result.success, qPrintable(result.errorMessage));

        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        QCOMPARE(file.readAll(), text.toUtf8());
    }

    void reportsWriteFailureWithFileErrorDetail()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString filePath = dir.filePath(QStringLiteral("missing/report.md"));

        const svm::report::TextFileWriteResult result = svm::report::writeUtf8TextFile(filePath, QStringLiteral("content"));

        QVERIFY(!result.success);
        const QString expectedPrefix = QStringLiteral("无法打开文件 %1：").arg(filePath);
        QVERIFY2(result.errorMessage.startsWith(expectedPrefix), qPrintable(result.errorMessage));
        QVERIFY(result.errorMessage.size() > expectedPrefix.size() + 1);
    }
};

QTEST_MAIN(RuleVerificationReportTests)
#include "rule_verification_report_tests.moc"
