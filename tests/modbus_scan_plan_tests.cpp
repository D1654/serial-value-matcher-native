#include <QtTest/QtTest>

#include "modbus/modbus_scan_plan.h"

class ModbusScanPlanTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsSingleBlockPlanForSmallRange() {
        svm::modbus::ScanPlanOptions options;
        options.slaveId = 1;
        options.functionCode = static_cast<quint8>(svm::modbus::ModbusReadFunction::HoldingRegisters);
        options.range = {0x006B, 0x006D};
        options.blockSize = 32;
        options.requestIntervalMs = 30;
        options.retryCount = 1;

        const auto result = svm::modbus::buildScanPlan(options);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.status, svm::modbus::ScanPlanBuildStatus::Success);
        QCOMPARE(result.plan.registerCount(), 3);
        QCOMPARE(result.plan.requestCount(), 1);
        QCOMPARE(result.plan.estimatedAttemptCount(), 2);
        QCOMPARE(result.plan.estimatedInterRequestDelayMs(), 0);
        QCOMPARE(result.plan.blocks.size(), 1);
        QCOMPARE(result.plan.blocks[0].index, 0);
        QCOMPARE(result.plan.blocks[0].startAddress, 0x006B);
        QCOMPARE(result.plan.blocks[0].endAddress, 0x006D);
        QCOMPARE(result.plan.blocks[0].quantity, 3);
        QCOMPARE(result.plan.blocks[0].requestFrame, QByteArray::fromHex("0103006B00037417"));
    }

    void splitsLargeRangeIntoSafeBlocks() {
        svm::modbus::ScanPlanOptions options;
        options.slaveId = 17;
        options.functionCode = static_cast<quint8>(svm::modbus::ModbusReadFunction::InputRegisters);
        options.range = {0, 70};
        options.blockSize = 32;
        options.requestIntervalMs = 25;
        options.retryCount = 2;
        options.safetyLevel = svm::modbus::ScanSafetyLevel::Balanced;

        const auto result = svm::modbus::buildScanPlan(options);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.plan.registerCount(), 71);
        QCOMPARE(result.plan.requestCount(), 3);
        QCOMPARE(result.plan.estimatedAttemptCount(), 9);
        QCOMPARE(result.plan.estimatedInterRequestDelayMs(), 50);
        QCOMPARE(result.plan.blocks[0].startAddress, 0);
        QCOMPARE(result.plan.blocks[0].endAddress, 31);
        QCOMPARE(result.plan.blocks[0].quantity, 32);
        QCOMPARE(result.plan.blocks[1].startAddress, 32);
        QCOMPARE(result.plan.blocks[1].endAddress, 63);
        QCOMPARE(result.plan.blocks[1].quantity, 32);
        QCOMPARE(result.plan.blocks[2].startAddress, 64);
        QCOMPARE(result.plan.blocks[2].endAddress, 70);
        QCOMPARE(result.plan.blocks[2].quantity, 7);
        QVERIFY(!result.plan.blocks[0].requestFrame.isEmpty());
        QVERIFY(!result.plan.blocks[2].requestFrame.isEmpty());
    }

    void acceptsAddressBoundaryAtLastRegister() {
        svm::modbus::ScanPlanOptions options;
        options.slaveId = 2;
        options.functionCode = static_cast<quint8>(svm::modbus::ModbusReadFunction::HoldingRegisters);
        options.range = {0xFFFF, 0xFFFF};
        options.blockSize = 1;

        const auto result = svm::modbus::buildScanPlan(options);

        QVERIFY2(result.ok, qPrintable(result.errorMessage));
        QCOMPARE(result.plan.blocks.size(), 1);
        QCOMPARE(result.plan.blocks[0].startAddress, 0xFFFF);
        QCOMPARE(result.plan.blocks[0].endAddress, 0xFFFF);
        QCOMPARE(result.plan.blocks[0].quantity, 1);
    }

    void rejectsReverseAddressRange() {
        svm::modbus::ScanPlanOptions options;
        options.range = {10, 9};

        const auto result = svm::modbus::buildScanPlan(options);

        QVERIFY(!result.ok);
        QCOMPARE(result.status, svm::modbus::ScanPlanBuildStatus::InvalidAddressRange);
        QVERIFY(result.errorMessage.contains(QStringLiteral("结束地址不能小于起始地址")));
    }

    void rejectsBlockSizeOutsideSafeBoundary() {
        svm::modbus::ScanPlanOptions zero;
        zero.blockSize = 0;
        const auto zeroResult = svm::modbus::buildScanPlan(zero);

        svm::modbus::ScanPlanOptions tooLarge;
        tooLarge.blockSize = 65;
        const auto tooLargeResult = svm::modbus::buildScanPlan(tooLarge);

        QVERIFY(!zeroResult.ok);
        QVERIFY(!tooLargeResult.ok);
        QCOMPARE(zeroResult.status, svm::modbus::ScanPlanBuildStatus::InvalidBlockSize);
        QCOMPARE(tooLargeResult.status, svm::modbus::ScanPlanBuildStatus::InvalidBlockSize);
        QVERIFY(zeroResult.errorMessage.contains(QStringLiteral("块大小")));
        QVERIFY(tooLargeResult.errorMessage.contains(QStringLiteral("1-64")));
    }

    void rejectsUnsafeHugePlan() {
        svm::modbus::ScanPlanOptions options;
        options.range = {0, 4096};
        options.blockSize = 64;

        const auto result = svm::modbus::buildScanPlan(options);

        QVERIFY(!result.ok);
        QCOMPARE(result.status, svm::modbus::ScanPlanBuildStatus::PlanTooLarge);
        QVERIFY(result.errorMessage.contains(QStringLiteral("最多允许 4096 个寄存器")));
    }

    void rejectsInvalidReadFunctionAndBroadcastSlaveId() {
        svm::modbus::ScanPlanOptions invalidFunction;
        invalidFunction.functionCode = 0x06;
        const auto invalidFunctionResult = svm::modbus::buildScanPlan(invalidFunction);

        svm::modbus::ScanPlanOptions broadcast;
        broadcast.slaveId = 0;
        const auto broadcastResult = svm::modbus::buildScanPlan(broadcast);

        QVERIFY(!invalidFunctionResult.ok);
        QCOMPARE(invalidFunctionResult.status, svm::modbus::ScanPlanBuildStatus::UnsupportedFunction);
        QVERIFY(invalidFunctionResult.errorMessage.contains(QStringLiteral("只允许 FC03/FC04")));
        QVERIFY(!broadcastResult.ok);
        QCOMPARE(broadcastResult.status, svm::modbus::ScanPlanBuildStatus::InvalidSlaveId);
        QVERIFY(broadcastResult.errorMessage.contains(QStringLiteral("广播地址 0")));
    }

    void rejectsInvalidRequestIntervalAndRetryCountWithStableStatus() {
        svm::modbus::ScanPlanOptions invalidInterval;
        invalidInterval.requestIntervalMs = -1;
        const auto invalidIntervalResult = svm::modbus::buildScanPlan(invalidInterval);

        svm::modbus::ScanPlanOptions invalidRetry;
        invalidRetry.retryCount = 6;
        const auto invalidRetryResult = svm::modbus::buildScanPlan(invalidRetry);

        QVERIFY(!invalidIntervalResult.ok);
        QCOMPARE(invalidIntervalResult.status, svm::modbus::ScanPlanBuildStatus::InvalidRequestInterval);
        QVERIFY(!invalidRetryResult.ok);
        QCOMPARE(invalidRetryResult.status, svm::modbus::ScanPlanBuildStatus::InvalidRetryCount);
        QCOMPARE(
            svm::modbus::describeScanPlanBuildStatus(invalidRetryResult.status),
            QStringLiteral("重试次数无效"));
    }

    void describesSafetyLevelsInChinese() {
        QVERIFY(svm::modbus::describeScanSafetyLevel(svm::modbus::ScanSafetyLevel::Conservative).contains(QStringLiteral("保守")));
        QVERIFY(svm::modbus::describeScanSafetyLevel(svm::modbus::ScanSafetyLevel::Balanced).contains(QStringLiteral("均衡")));
        QVERIFY(svm::modbus::describeScanSafetyLevel(svm::modbus::ScanSafetyLevel::Aggressive).contains(QStringLiteral("激进")));
        QVERIFY(svm::modbus::describeScanSafetyLevel(svm::modbus::ScanSafetyLevel::Custom).contains(QStringLiteral("自定义")));
    }
};

QTEST_MAIN(ModbusScanPlanTests)
#include "modbus_scan_plan_tests.moc"
