#include <QtTest/QtTest>
#include <QElapsedTimer>

#include "core/modbus_scan_executor_core.h"
#include "matching/candidate_stability_analyzer.h"
#include "matching/protocol_rule_verifier.h"
#include "matching/value_candidate_generator.h"
#include "storage/protocol_rule_records.h"
#include "transport/serial_write_queue.h"

#include <cstdint>
#include <vector>

using namespace svm::matching;

namespace {

RegisterSample sample(int address, quint16 value)
{
    RegisterSample result;
    result.observationId = 100000 + address;
    result.sessionId = QStringLiteral("quality-scan");
    result.slaveId = 1;
    result.functionCode = 3;
    result.address = address;
    result.value = value;
    result.blockIndex = address / 64;
    result.attemptIndex = 0;
    result.observedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs).addMSecs(address);
    return result;
}

TargetValue broadTarget()
{
    TargetValue target;
    target.label = QStringLiteral("质量回归目标");
    target.value = 12345.0;
    target.unit = QStringLiteral("unit");
    target.sampledAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs);
    return target;
}

svm::storage::ProtocolFieldRuleRecord uint16Rule(int index)
{
    svm::storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = QStringLiteral("quality-rule-%1").arg(index);
    rule.fieldName = QStringLiteral("字段%1").arg(index);
    rule.sourceStabilityRunId = QStringLiteral("quality-stability");
    rule.sourceStableCandidateId = index;
    rule.candidateType = QStringLiteral("UInt16");
    rule.wordOrder = QStringLiteral("HighWordFirst");
    rule.byteOrder = QStringLiteral("BigEndian");
    rule.slaveId = 1;
    rule.functionCode = 3;
    rule.startAddress = index;
    rule.registerCount = 1;
    rule.scaleMultiplier = 1.0;
    rule.scaleOffset = 0.0;
    rule.confidenceLevel = QStringLiteral("高");
    rule.stabilityScore = 90.0;
    rule.evidenceSummary = QStringLiteral("质量回归规则");
    rule.createdAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs);
    return rule;
}

CandidateObservation stabilityObservation(int runIndex, int candidateIndex)
{
    CandidateObservation observation;
    observation.runId = QStringLiteral("quality-match-%1").arg(runIndex);
    observation.sourceScanSessionId = QStringLiteral("quality-scan-%1").arg(runIndex);
    observation.candidateType = QStringLiteral("UInt16");
    observation.wordOrder = QStringLiteral("HighWordFirst");
    observation.byteOrder = QStringLiteral("BigEndian");
    observation.sourceSessionId = observation.sourceScanSessionId;
    observation.slaveId = 1;
    observation.functionCode = 3;
    observation.startAddress = candidateIndex;
    observation.registerCount = 1;
    observation.scaleMultiplier = 1.0;
    observation.scaleOffset = 0.0;
    observation.targetValue = 1000.0 + runIndex;
    observation.engineeringValue = observation.targetValue + 0.01;
    observation.absoluteError = 0.01;
    observation.effectiveTolerance = 1.0;
    observation.candidateScore = 99.0;
    observation.observationIds = {static_cast<qint64>(runIndex * 10000 + candidateIndex)};
    observation.addresses = {candidateIndex};
    return observation;
}

svm::core::ByteBuffer modbusReadResponse(
    int slaveId,
    svm::core::Byte functionCode,
    int startAddress,
    int quantity)
{
    svm::core::ByteBuffer body;
    body.push_back(static_cast<svm::core::Byte>(slaveId));
    body.push_back(functionCode);
    body.push_back(static_cast<svm::core::Byte>(quantity * 2));
    for (int offset = 0; offset < quantity; ++offset) {
        const auto value = static_cast<std::uint16_t>(startAddress + offset);
        body.push_back(static_cast<svm::core::Byte>((value >> 8) & 0xFF));
        body.push_back(static_cast<svm::core::Byte>(value & 0xFF));
    }
    return svm::core::modbus::appendCrc16Modbus(body);
}

class QualityRegressionModbusTransport final : public svm::core::modbus::RtuTransport {
public:
    svm::core::modbus::RtuTransportExchange exchange(svm::core::ByteSpan requestFrame, int responseTimeoutMs) override
    {
        Q_UNUSED(responseTimeoutMs);
        ++requestCount;
        svm::core::modbus::RtuTransportExchange result;
        result.status = svm::core::modbus::RtuTransportExchangeStatus::Success;
        result.requestFrame = svm::core::ByteBuffer(requestFrame.begin(), requestFrame.end());
        result.sentAtUtc = "2026-06-12T00:00:00Z";
        result.receivedAtUtc = "2026-06-12T00:00:00Z";
        result.endpoint = "quality://modbus";

        if (requestFrame.size() < 6) {
            result.status = svm::core::modbus::RtuTransportExchangeStatus::TransportError;
            result.errorMessage = "bad request";
            return result;
        }

        const int slaveId = requestFrame[0];
        const svm::core::Byte functionCode = requestFrame[1];
        const int startAddress = (static_cast<int>(requestFrame[2]) << 8) | static_cast<int>(requestFrame[3]);
        const int quantity = (static_cast<int>(requestFrame[4]) << 8) | static_cast<int>(requestFrame[5]);
        result.responseFrame = modbusReadResponse(slaveId, functionCode, startAddress, quantity);
        return result;
    }

    int requestCount = 0;
};

} // namespace

class QualityRegressionTests final : public QObject {
    Q_OBJECT

private slots:
    void candidateGenerationKeepsBoundOnDenseLargeInput()
    {
        QList<RegisterSample> samples;
        samples.reserve(2048);
        for (int address = 0; address < 2048; ++address) {
            samples.append(sample(address, static_cast<quint16>(address)));
        }

        CandidateGenerationOptions options;
        options.tolerance.absolute = 1000000000.0;
        options.maxCandidates = 40;

        QElapsedTimer timer;
        timer.start();
        const CandidateGenerationResult result = generateValueCandidates(samples, broadTarget(), options);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QVERIFY(result.candidates.size() <= options.maxCandidates);
        QVERIFY2(timer.elapsed() < 5000, "候选生成大样本回归超过 5 秒，可能存在性能退化。");
    }

    void ruleVerificationHandlesManyRulesWithIndexedSamples()
    {
        QList<RegisterSample> samples;
        QList<svm::storage::ProtocolFieldRuleRecord> rules;
        samples.reserve(4096);
        rules.reserve(1500);
        for (int address = 0; address < 4096; ++address) {
            samples.append(sample(address, static_cast<quint16>(address)));
            if (address < 1500) {
                rules.append(uint16Rule(address));
            }
        }

        QElapsedTimer timer;
        timer.start();
        const ProtocolRuleVerificationSummary summary = verifyProtocolFieldRules(rules, samples);

        QCOMPARE(summary.totalRules, rules.size());
        QCOMPARE(summary.verifiedRules, rules.size());
        QCOMPARE(summary.missingRules, 0);
        QCOMPARE(summary.unsupportedRules, 0);
        QVERIFY2(timer.elapsed() < 3000, "规则验证大样本回归超过 3 秒，索引路径可能退化。");
    }

    void stabilityAnalysisHandlesManyRunsAndCandidateGroups()
    {
        QList<CandidateObservation> observations;
        observations.reserve(2000);
        for (int runIndex = 0; runIndex < 20; ++runIndex) {
            for (int candidateIndex = 0; candidateIndex < 100; ++candidateIndex) {
                observations.append(stabilityObservation(runIndex, candidateIndex));
            }
        }

        StabilityAnalysisOptions options;
        options.minimumSampleCount = 2;
        options.strongSampleCount = 10;

        QElapsedTimer timer;
        timer.start();
        const StabilityAnalysisResult result = analyzeCandidateStability(observations, options);

        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.candidates.size(), 100);
        QVERIFY(result.candidates.first().sampleCount >= options.strongSampleCount);
        QVERIFY2(timer.elapsed() < 3000, "稳定性分析大样本回归超过 3 秒，分组路径可能退化。");
    }

    void serialWriteQueueKeepsBoundedThroughputForLargeBurst()
    {
        svm::transport::SerialWriteQueue queue(256);

        QElapsedTimer timer;
        timer.start();
        for (int batch = 0; batch < 32; ++batch) {
            for (int index = 0; index < 256; ++index) {
                const auto accepted = queue.enqueue({
                    static_cast<std::uint8_t>(index & 0xFF),
                    static_cast<std::uint8_t>((index >> 8) & 0xFF),
                }, 250);
                QVERIFY(accepted.status == svm::transport::SerialWriteResultStatus::Accepted);
            }
            QVERIFY(queue.pendingCount() == std::size_t{256});
            for (int index = 0; index < 256; ++index) {
                const auto sent = queue.completeNextSent(2);
                QVERIFY(sent.status == svm::transport::SerialWriteResultStatus::Sent);
                QVERIFY(sent.terminal());
            }
            QVERIFY(queue.empty());
        }

        QVERIFY2(timer.elapsed() < 1000, "串口写队列大批量入队/完成回归超过 1 秒，队列路径可能退化。");
    }

    void coreModbusScanExecutorKeepsMultiBlockExecutionBounded()
    {
        svm::core::modbus::ScanPlanOptions planOptions;
        planOptions.slaveId = 1;
        planOptions.functionCode = static_cast<svm::core::Byte>(svm::core::modbus::ModbusReadFunction::HoldingRegisters);
        planOptions.range = {0, 511};
        planOptions.blockSize = 16;
        planOptions.requestIntervalMs = 0;
        planOptions.retryCount = 0;
        const auto planResult = svm::core::modbus::buildScanPlan(planOptions);
        QVERIFY2(planResult.ok, planResult.errorMessage.c_str());
        QVERIFY(planResult.plan.blocks.size() == std::size_t{32});

        QualityRegressionModbusTransport transport;
        svm::core::modbus::ScanExecutionOptions executionOptions;
        executionOptions.responseTimeoutMs = 250;
        executionOptions.nowUtc = [] {
            return svm::core::Text("2026-06-12T00:00:00Z");
        };

        QElapsedTimer timer;
        timer.start();
        svm::core::modbus::ScanExecutor executor(transport);
        const auto result = executor.execute(planResult.plan, executionOptions);

        QVERIFY(result.status == svm::core::modbus::ScanExecutionStatus::Completed);
        QCOMPARE(result.successBlockCount, 32);
        QCOMPARE(result.failedBlockCount, 0);
        QVERIFY(result.observations.size() == std::size_t{512});
        QCOMPARE(transport.requestCount, 32);
        QVERIFY2(timer.elapsed() < 1000, "Modbus 扫描 executor 多块回归超过 1 秒，执行路径可能退化。");
    }
};

QTEST_MAIN(QualityRegressionTests)
#include "quality_regression_tests.moc"
