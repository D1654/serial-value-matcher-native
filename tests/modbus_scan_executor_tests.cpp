#include <QtTest/QtTest>

#include "modbus/modbus_rtu_codec.h"
#include "modbus/modbus_rtu_transport.h"
#include "modbus/modbus_scan_executor.h"
#include "modbus/modbus_scan_plan.h"

#include <utility>

namespace {

class FakeModbusRtuTransport final : public svm::modbus::ModbusRtuTransport {
public:
    struct QueuedExchange {
        QByteArray expectedRequest;
        svm::modbus::ModbusTransportExchange exchange;
    };

    void enqueueResponse(QByteArray expectedRequest, QByteArray responseFrame, QString endpoint = QStringLiteral("fake://modbus")) {
        svm::modbus::ModbusTransportExchange exchange;
        exchange.status = svm::modbus::ModbusTransportStatus::Success;
        exchange.responseFrame = std::move(responseFrame);
        exchange.sentAtUtc = QDateTime::currentDateTimeUtc();
        exchange.receivedAtUtc = exchange.sentAtUtc.addMSecs(5);
        exchange.endpoint = std::move(endpoint);
        exchanges_.append(QueuedExchange{std::move(expectedRequest), exchange});
    }

    void enqueueTimeout(QByteArray expectedRequest) {
        svm::modbus::ModbusTransportExchange exchange;
        exchange.status = svm::modbus::ModbusTransportStatus::Timeout;
        exchange.errorMessage = QStringLiteral("测试超时");
        exchange.sentAtUtc = QDateTime(QDate(2026, 6, 3), QTime(1, 1, 0), Qt::UTC);
        exchanges_.append(QueuedExchange{std::move(expectedRequest), exchange});
    }

    void enqueueTransportError(QByteArray expectedRequest, QString message = QStringLiteral("串口未打开")) {
        svm::modbus::ModbusTransportExchange exchange;
        exchange.status = svm::modbus::ModbusTransportStatus::TransportError;
        exchange.errorMessage = std::move(message);
        exchange.sentAtUtc = QDateTime(QDate(2026, 6, 3), QTime(1, 2, 0), Qt::UTC);
        exchanges_.append(QueuedExchange{std::move(expectedRequest), exchange});
    }

    svm::modbus::ModbusTransportExchange exchange(const QByteArray& requestFrame, int responseTimeoutMs) override {
        sentRequests_.append(requestFrame);
        timeoutValues_.append(responseTimeoutMs);

        if (exchanges_.isEmpty()) {
            svm::modbus::ModbusTransportExchange fallback;
            fallback.status = svm::modbus::ModbusTransportStatus::TransportError;
            fallback.requestFrame = requestFrame;
            fallback.errorMessage = QStringLiteral("没有为该请求配置假响应。");
            return fallback;
        }

        auto queued = exchanges_.takeFirst();
        if (queued.expectedRequest != requestFrame) {
            svm::modbus::ModbusTransportExchange mismatch;
            mismatch.status = svm::modbus::ModbusTransportStatus::TransportError;
            mismatch.requestFrame = requestFrame;
            mismatch.errorMessage = QStringLiteral("请求顺序不匹配。期望 %1，实际 %2。")
                .arg(QString::fromLatin1(queued.expectedRequest.toHex(' ').toUpper()))
                .arg(QString::fromLatin1(requestFrame.toHex(' ').toUpper()));
            return mismatch;
        }

        queued.exchange.requestFrame = requestFrame;
        return queued.exchange;
    }

    QVector<QByteArray> sentRequests() const { return sentRequests_; }
    QVector<int> timeoutValues() const { return timeoutValues_; }

private:
    QVector<QueuedExchange> exchanges_;
    QVector<QByteArray> sentRequests_;
    QVector<int> timeoutValues_;
};

QByteArray normalReadResponse(int slaveId, int functionCode, std::initializer_list<quint16> values) {
    QByteArray body;
    body.append(static_cast<char>(slaveId));
    body.append(static_cast<char>(functionCode));
    body.append(static_cast<char>(values.size() * 2));
    for (const auto value : values) {
        body.append(static_cast<char>((value >> 8) & 0xFF));
        body.append(static_cast<char>(value & 0xFF));
    }
    return svm::modbus::appendCrc16Modbus(body);
}

QByteArray exceptionResponse(int slaveId, int functionCode, int exceptionCode) {
    QByteArray body;
    body.append(static_cast<char>(slaveId));
    body.append(static_cast<char>(functionCode | 0x80));
    body.append(static_cast<char>(exceptionCode));
    return svm::modbus::appendCrc16Modbus(body);
}

svm::modbus::ScanPlan makePlan(int startAddress, int endAddress, int blockSize, int retryCount = 0) {
    svm::modbus::ScanPlanOptions options;
    options.slaveId = 1;
    options.functionCode = static_cast<quint8>(svm::modbus::ModbusReadFunction::HoldingRegisters);
    options.range = {startAddress, endAddress};
    options.blockSize = blockSize;
    options.retryCount = retryCount;
    const auto plan = svm::modbus::buildScanPlan(options);
    Q_ASSERT(plan.ok);
    return plan.plan;
}

} // namespace

class ModbusScanExecutorTests final : public QObject {
    Q_OBJECT

private slots:
    void executesSingleBlockScanAndKeepsRawFramesTraceable() {
        const auto plan = makePlan(0x006B, 0x006D, 32);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {0x022B, 0x0000, 0x0064}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);
        QCOMPARE(result.successBlockCount, 1);
        QCOMPARE(result.failedBlockCount, 0);
        QCOMPARE(result.observations.size(), 3);
        QCOMPARE(result.observations[0].address, static_cast<quint16>(0x006B));
        QCOMPARE(result.observations[0].value, static_cast<quint16>(0x022B));
        QCOMPARE(result.observations[2].address, static_cast<quint16>(0x006D));
        QCOMPARE(result.blocks[0].attempts[0].requestFrame, plan.blocks[0].requestFrame);
        QCOMPARE(result.blocks[0].attempts[0].responseFrame, normalReadResponse(1, 0x03, {0x022B, 0x0000, 0x0064}));
        QCOMPARE(transport.sentRequests(), QVector<QByteArray>({plan.blocks[0].requestFrame}));
    }

    void executesMultipleBlocksInPlanOrder() {
        const auto plan = makePlan(0, 4, 2);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {10, 11}));
        transport.enqueueResponse(plan.blocks[1].requestFrame, normalReadResponse(1, 0x03, {12, 13}));
        transport.enqueueResponse(plan.blocks[2].requestFrame, normalReadResponse(1, 0x03, {14}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);
        QCOMPARE(result.blocks.size(), 3);
        QCOMPARE(result.observations.size(), 5);
        for (int index = 0; index < result.observations.size(); ++index) {
            QCOMPARE(result.observations[index].address, static_cast<quint16>(index));
            QCOMPARE(result.observations[index].value, static_cast<quint16>(10 + index));
        }
        QCOMPARE(transport.sentRequests(), QVector<QByteArray>({
            plan.blocks[0].requestFrame,
            plan.blocks[1].requestFrame,
            plan.blocks[2].requestFrame
        }));
    }

    void recordsModbusExceptionAndContinuesFollowingBlocks() {
        const auto plan = makePlan(0, 5, 2);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {1, 2}));
        transport.enqueueResponse(plan.blocks[1].requestFrame, exceptionResponse(1, 0x03, 0x02));
        transport.enqueueResponse(plan.blocks[2].requestFrame, normalReadResponse(1, 0x03, {5, 6}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::CompletedWithErrors);
        QCOMPARE(result.successBlockCount, 2);
        QCOMPARE(result.failedBlockCount, 1);
        QCOMPARE(result.observations.size(), 4);
        QCOMPARE(result.blocks[1].attempts[0].status, svm::modbus::ScanAttemptStatus::ModbusException);
        QVERIFY(result.blocks[1].attempts[0].isModbusException);
        QCOMPARE(result.blocks[1].attempts[0].exceptionCode, static_cast<quint8>(0x02));
        QCOMPARE(result.blocks[1].attempts[0].exceptionDescription, QStringLiteral("非法数据地址"));
        QVERIFY(!result.blocks[1].attempts[0].requestFrame.isEmpty());
        QVERIFY(!result.blocks[1].attempts[0].responseFrame.isEmpty());
        QCOMPARE(transport.sentRequests().size(), 3);
    }

    void recordsCrcMismatchAsParseErrorWithRawResponse() {
        const auto plan = makePlan(0, 1, 2);
        QByteArray badCrc = normalReadResponse(1, 0x03, {1, 2});
        badCrc[badCrc.size() - 1] = static_cast<char>(0x00);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, badCrc);
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QCOMPARE(result.blocks[0].attempts[0].status, svm::modbus::ScanAttemptStatus::ParseError);
        QVERIFY(result.blocks[0].attempts[0].errorMessage.contains(QStringLiteral("CRC")));
        QCOMPARE(result.blocks[0].attempts[0].responseFrame, badCrc);
    }

    void recordsSlaveIdMismatchAsParseError() {
        const auto plan = makePlan(0, 0, 1);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(2, 0x03, {1}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QCOMPARE(result.blocks[0].attempts[0].status, svm::modbus::ScanAttemptStatus::ParseError);
        QVERIFY(result.blocks[0].attempts[0].errorMessage.contains(QStringLiteral("从站 ID 不匹配")));
        QVERIFY(!result.blocks[0].attempts[0].requestFrame.isEmpty());
        QVERIFY(!result.blocks[0].attempts[0].responseFrame.isEmpty());
    }

    void recordsFunctionCodeMismatchAsParseError() {
        const auto plan = makePlan(0, 0, 1);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x04, {1}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QCOMPARE(result.blocks[0].attempts[0].status, svm::modbus::ScanAttemptStatus::ParseError);
        QVERIFY(result.blocks[0].attempts[0].errorMessage.contains(QStringLiteral("功能码不匹配")));
    }

    void recordsTimeoutAndKeepsRequestFrame() {
        const auto plan = makePlan(0, 2, 1);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {1}));
        transport.enqueueTimeout(plan.blocks[1].requestFrame);
        transport.enqueueResponse(plan.blocks[2].requestFrame, normalReadResponse(1, 0x03, {3}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::CompletedWithErrors);
        QCOMPARE(result.blocks[1].attempts[0].status, svm::modbus::ScanAttemptStatus::Timeout);
        QCOMPARE(result.blocks[1].attempts[0].requestFrame, plan.blocks[1].requestFrame);
        QVERIFY(result.blocks[1].attempts[0].responseFrame.isEmpty());
        QCOMPARE(result.observations.size(), 2);
        QCOMPARE(transport.sentRequests().size(), 3);
    }

    void recordsTransportErrorWithoutParsingResponse() {
        const auto plan = makePlan(0, 0, 1);
        FakeModbusRtuTransport transport;
        transport.enqueueTransportError(plan.blocks[0].requestFrame, QStringLiteral("串口未打开"));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QCOMPARE(result.blocks[0].attempts[0].status, svm::modbus::ScanAttemptStatus::TransportError);
        QVERIFY(result.blocks[0].attempts[0].errorMessage.contains(QStringLiteral("串口未打开")));
        QVERIFY(result.blocks[0].attempts[0].responseFrame.isEmpty());
    }

    void retriesTimeoutThenUsesSuccessfulAttemptObservations() {
        const auto plan = makePlan(0, 0, 1, 1);
        FakeModbusRtuTransport transport;
        transport.enqueueTimeout(plan.blocks[0].requestFrame);
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {42}));
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);
        QCOMPARE(result.blocks[0].attempts.size(), 2);
        QCOMPARE(result.blocks[0].attempts[0].status, svm::modbus::ScanAttemptStatus::Timeout);
        QCOMPARE(result.blocks[0].attempts[1].status, svm::modbus::ScanAttemptStatus::Success);
        QCOMPARE(result.observations.size(), 1);
        QCOMPARE(result.observations[0].attemptIndex, 1);
        QCOMPARE(result.observations[0].value, static_cast<quint16>(42));
    }

    void recordsAllRetryFailures() {
        const auto plan = makePlan(0, 0, 1, 2);
        FakeModbusRtuTransport transport;
        transport.enqueueTimeout(plan.blocks[0].requestFrame);
        transport.enqueueTimeout(plan.blocks[0].requestFrame);
        transport.enqueueTimeout(plan.blocks[0].requestFrame);
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(plan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QCOMPARE(result.blocks[0].attempts.size(), 3);
        QCOMPARE(result.failedBlockCount, 1);
        QCOMPARE(result.observations.size(), 0);
    }

    void rejectsInvalidEmptyPlanWithoutCrashing() {
        svm::modbus::ScanPlan emptyPlan;
        emptyPlan.slaveId = 1;
        emptyPlan.functionCode = 0x03;
        FakeModbusRtuTransport transport;
        svm::modbus::ModbusScanExecutor executor(transport);

        const auto result = executor.execute(emptyPlan);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QVERIFY(result.errorMessage.contains(QStringLiteral("没有任何请求块")));
        QCOMPARE(transport.sentRequests().size(), 0);
    }

    void passesTimeoutOptionToTransport() {
        const auto plan = makePlan(0, 0, 1);
        FakeModbusRtuTransport transport;
        transport.enqueueResponse(plan.blocks[0].requestFrame, normalReadResponse(1, 0x03, {7}));
        svm::modbus::ModbusScanExecutor executor(transport);
        svm::modbus::ScanExecutionOptions options;
        options.responseTimeoutMs = 250;

        const auto result = executor.execute(plan, options);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);
        QCOMPARE(transport.timeoutValues(), QVector<int>({250}));
    }

    void cancellationStopsBeforeSendingRequests() {
        const auto plan = makePlan(0, 2, 3);
        FakeModbusRtuTransport transport;
        svm::modbus::ModbusScanExecutor executor(transport);
        svm::modbus::ScanExecutionOptions options;
        options.shouldCancel = []() {
            return true;
        };

        const auto result = executor.execute(plan, options);

        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Failed);
        QVERIFY(result.errorMessage.contains(QStringLiteral("扫描已取消")));
        QVERIFY(result.blocks.isEmpty());
        QVERIFY(transport.sentRequests().isEmpty());
    }
};

QTEST_MAIN(ModbusScanExecutorTests)
#include "modbus_scan_executor_tests.moc"
