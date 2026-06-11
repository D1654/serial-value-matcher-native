#include <QtTest/QtTest>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "modbus/modbus_rtu_codec.h"
#include "modbus/modbus_rtu_transport.h"
#include "modbus/modbus_scan_executor.h"
#include "modbus/modbus_scan_plan.h"
#include "storage/session_store.h"
#include "matching/scan_observation_adapter.h"
#include "matching/value_candidate_generator.h"

#include <utility>

namespace {

class FakeModbusRtuTransport final : public svm::modbus::ModbusRtuTransport {
public:
    void enqueue(QByteArray expectedRequest, QByteArray responseFrame) {
        responses_.append({std::move(expectedRequest), std::move(responseFrame)});
    }

    svm::modbus::ModbusTransportExchange exchange(const QByteArray& requestFrame, int responseTimeoutMs) override {
        Q_UNUSED(responseTimeoutMs);
        svm::modbus::ModbusTransportExchange exchange;
        exchange.requestFrame = requestFrame;
        exchange.sentAtUtc = QDateTime::currentDateTimeUtc();
        exchange.receivedAtUtc = exchange.sentAtUtc.addMSecs(5);
        exchange.endpoint = QStringLiteral("fake://persist-test");

        if (responses_.isEmpty()) {
            exchange.status = svm::modbus::ModbusTransportStatus::TransportError;
            exchange.errorMessage = QStringLiteral("没有配置假响应。");
            return exchange;
        }

        auto next = responses_.takeFirst();
        if (next.expectedRequest != requestFrame) {
            exchange.status = svm::modbus::ModbusTransportStatus::TransportError;
            exchange.errorMessage = QStringLiteral("请求顺序不匹配。");
            return exchange;
        }

        exchange.status = svm::modbus::ModbusTransportStatus::Success;
        exchange.responseFrame = next.responseFrame;
        return exchange;
    }

private:
    struct QueuedResponse {
        QByteArray expectedRequest;
        QByteArray responseFrame;
    };

    QVector<QueuedResponse> responses_;
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

QString executionStatusName(svm::modbus::ScanExecutionStatus status) {
    switch (status) {
    case svm::modbus::ScanExecutionStatus::Completed:
        return QStringLiteral("Completed");
    case svm::modbus::ScanExecutionStatus::CompletedWithErrors:
        return QStringLiteral("CompletedWithErrors");
    case svm::modbus::ScanExecutionStatus::Failed:
        return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QString attemptStatusName(svm::modbus::ScanAttemptStatus status) {
    switch (status) {
    case svm::modbus::ScanAttemptStatus::Success:
        return QStringLiteral("Success");
    case svm::modbus::ScanAttemptStatus::ModbusException:
        return QStringLiteral("ModbusException");
    case svm::modbus::ScanAttemptStatus::ParseError:
        return QStringLiteral("ParseError");
    case svm::modbus::ScanAttemptStatus::Timeout:
        return QStringLiteral("Timeout");
    case svm::modbus::ScanAttemptStatus::TransportError:
        return QStringLiteral("TransportError");
    }
    return QStringLiteral("Unknown");
}

svm::storage::ScanExecutionPersistenceRecord toPersistence(
    const QString& sessionId,
    const svm::modbus::ScanExecutionResult& result) {
    svm::storage::ScanExecutionPersistenceRecord persistence;
    persistence.session.sessionId = sessionId;
    persistence.session.slaveId = result.plan.slaveId;
    persistence.session.functionCode = result.plan.functionCode;
    persistence.session.startAddress = result.plan.range.startAddress;
    persistence.session.endAddress = result.plan.range.endAddress;
    persistence.session.blockSize = result.plan.blockSize;
    persistence.session.requestCount = result.plan.requestCount();
    persistence.session.status = executionStatusName(result.status);
    persistence.session.startedAtUtc = result.startedAtUtc;
    persistence.session.finishedAtUtc = result.finishedAtUtc;
    persistence.session.successBlockCount = result.successBlockCount;
    persistence.session.failedBlockCount = result.failedBlockCount;
    persistence.session.errorMessage = result.errorMessage;

    for (const auto& block : result.blocks) {
        for (const auto& attempt : block.attempts) {
            svm::storage::ScanAttemptRecord record;
            record.sessionId = sessionId;
            record.blockIndex = attempt.blockIndex;
            record.attemptIndex = attempt.attemptIndex;
            record.startAddress = block.block.startAddress;
            record.quantity = block.block.quantity;
            record.status = attemptStatusName(attempt.status);
            record.requestFrame = attempt.requestFrame;
            record.responseFrame = attempt.responseFrame;
            record.errorMessage = attempt.errorMessage;
            record.isModbusException = attempt.isModbusException;
            record.exceptionCode = attempt.exceptionCode;
            record.exceptionDescription = attempt.exceptionDescription;
            record.sentAtUtc = attempt.sentAtUtc;
            record.receivedAtUtc = attempt.receivedAtUtc;
            record.endpoint = attempt.endpoint;
            persistence.attempts.append(record);
        }

        for (const auto& observation : block.observations) {
            svm::storage::ScanObservationRecord record;
            record.sessionId = sessionId;
            record.blockIndex = observation.blockIndex;
            record.attemptIndex = observation.attemptIndex;
            record.slaveId = observation.slaveId;
            record.functionCode = observation.functionCode;
            record.address = observation.address;
            record.value = observation.value;
            record.observedAtUtc = observation.observedAtUtc;
            persistence.observations.append(record);
        }
    }

    return persistence;
}

svm::modbus::ScanPlan makePlan(int startAddress, int endAddress, int blockSize) {
    svm::modbus::ScanPlanOptions options;
    options.slaveId = 1;
    options.functionCode = static_cast<quint8>(svm::modbus::ModbusReadFunction::HoldingRegisters);
    options.range = {startAddress, endAddress};
    options.blockSize = blockSize;
    const auto result = svm::modbus::buildScanPlan(options);
    Q_ASSERT(result.ok);
    return result.plan;
}

void dropTableForReadFailure(const QString& databasePath, const QString& tableName) {
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

class ModbusScanPersistenceTests final : public QObject {
    Q_OBJECT

private slots:
    void savesSuccessfulFakeScanAndReloadsFacts() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath(QStringLiteral("scan.sqlite"));

        const auto plan = makePlan(0, 2, 3);
        const QByteArray response = normalReadResponse(1, 0x03, {11, 12, 13});
        FakeModbusRtuTransport transport;
        transport.enqueue(plan.blocks[0].requestFrame, response);
        svm::modbus::ModbusScanExecutor executor(transport);
        const auto result = executor.execute(plan);
        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);

        svm::storage::SessionStore writer;
        QVERIFY2(writer.open(dbPath), qPrintable(writer.lastErrorText()));
        QVERIFY2(writer.saveScanExecution(toPersistence(QStringLiteral("scan-success"), result)), qPrintable(writer.lastErrorText()));

        svm::storage::SessionStore reader;
        QVERIFY2(reader.open(dbPath), qPrintable(reader.lastErrorText()));
        const auto latestSession = reader.latestScanSession();
        QVERIFY(latestSession.has_value());
        QCOMPARE(latestSession->sessionId, QStringLiteral("scan-success"));
        const auto recentSessions = reader.recentScanSessions();
        QCOMPARE(recentSessions.size(), 1);
        QCOMPARE(recentSessions.first().sessionId, QStringLiteral("scan-success"));

        const auto session = reader.scanSession(QStringLiteral("scan-success"));
        QVERIFY(session.has_value());
        QCOMPARE(session->status, QStringLiteral("Completed"));
        QCOMPARE(session->slaveId, 1);
        QCOMPARE(session->functionCode, 0x03);
        QCOMPARE(session->startAddress, 0);
        QCOMPARE(session->endAddress, 2);
        QCOMPARE(session->requestCount, 1);
        QCOMPARE(session->successBlockCount, 1);
        QCOMPARE(session->failedBlockCount, 0);

        const auto attempts = reader.scanAttempts(QStringLiteral("scan-success"));
        QCOMPARE(attempts.size(), 1);
        QCOMPARE(attempts[0].status, QStringLiteral("Success"));
        QCOMPARE(attempts[0].requestFrame, plan.blocks[0].requestFrame);
        QCOMPARE(attempts[0].responseFrame, response);
        QVERIFY(attempts[0].sentAtUtc.isValid());
        QVERIFY(attempts[0].receivedAtUtc.isValid());

        const auto observations = reader.scanObservations(QStringLiteral("scan-success"));
        QCOMPARE(observations.size(), 3);
        QCOMPARE(observations[0].address, 0);
        QCOMPARE(observations[0].value, 11);
        QCOMPARE(observations[2].address, 2);
        QCOMPARE(observations[2].value, 13);
        QVERIFY(observations[0].observedAtUtc.isValid());

        const auto selectedObservations = reader.scanObservationsByIds({observations[2].id, observations[0].id});
        QVERIFY(!reader.hasReadError());
        QCOMPARE(selectedObservations.size(), 2);
        QCOMPARE(selectedObservations[0].id, observations[0].id);
        QCOMPARE(selectedObservations[0].blockIndex, observations[0].blockIndex);
        QCOMPARE(selectedObservations[0].attemptIndex, observations[0].attemptIndex);
        QCOMPARE(selectedObservations[1].id, observations[2].id);
        QCOMPARE(selectedObservations[1].address, 2);

        const auto emptySelection = reader.scanObservationsByIds({});
        QVERIFY(emptySelection.isEmpty());
        QVERIFY(!reader.hasReadError());
    }

    void surfacesScanReadFailures() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath(QStringLiteral("scan-read-error.sqlite"));

        const auto plan = makePlan(0, 1, 2);
        const QByteArray response = normalReadResponse(1, 0x03, {31, 32});
        FakeModbusRtuTransport transport;
        transport.enqueue(plan.blocks[0].requestFrame, response);
        svm::modbus::ModbusScanExecutor executor(transport);
        const auto result = executor.execute(plan);
        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dbPath), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveScanExecution(toPersistence(QStringLiteral("scan-read-error"), result)), qPrintable(store.lastErrorText()));

        dropTableForReadFailure(dbPath, QStringLiteral("scan_attempts"));

        const auto attempts = store.scanAttempts(QStringLiteral("scan-read-error"));
        QVERIFY(attempts.isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取扫描请求尝试失败")));

        dropTableForReadFailure(dbPath, QStringLiteral("scan_observations"));

        const auto observations = store.scanObservations(QStringLiteral("scan-read-error"));
        QVERIFY(observations.isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("读取扫描观测失败")));

        const auto selectedObservations = store.scanObservationsByIds({1});
        QVERIFY(selectedObservations.isEmpty());
        QVERIFY(store.hasReadError());
        QVERIFY(store.lastReadErrorText().contains(QStringLiteral("按 ID 读取扫描观测失败")));
    }

    void savesPersistedScanObservationsCanGenerateValueCandidates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath(QStringLiteral("scan-to-candidates.sqlite"));

        const auto plan = makePlan(100, 101, 2);
        const QByteArray response = normalReadResponse(1, 0x03, {0x4145, 0x70A4});
        FakeModbusRtuTransport transport;
        transport.enqueue(plan.blocks[0].requestFrame, response);
        svm::modbus::ModbusScanExecutor executor(transport);
        const auto result = executor.execute(plan);
        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::Completed);

        svm::storage::SessionStore writer;
        QVERIFY2(writer.open(dbPath), qPrintable(writer.lastErrorText()));
        QVERIFY2(writer.saveScanExecution(toPersistence(QStringLiteral("scan-candidates"), result)), qPrintable(writer.lastErrorText()));

        svm::storage::SessionStore reader;
        QVERIFY2(reader.open(dbPath), qPrintable(reader.lastErrorText()));
        const auto observations = reader.scanObservations(QStringLiteral("scan-candidates"));
        QCOMPARE(observations.size(), 2);
        QVERIFY(observations[0].id > 0);
        QVERIFY(observations[1].id > 0);

        const QList<svm::matching::RegisterSample> samples = svm::matching::registerSamplesFromScanObservations(observations);
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[0].sessionId, QStringLiteral("scan-candidates"));
        QCOMPARE(samples[0].address, 100);
        QCOMPARE(samples[0].blockIndex, 0);
        QCOMPARE(samples[0].attemptIndex, 0);

        svm::matching::TargetValue target;
        target.label = QStringLiteral("温度");
        target.value = 12.34;
        target.unit = QStringLiteral("℃");
        target.sampledAtUtc = QDateTime::currentDateTimeUtc();

        svm::matching::CandidateGenerationOptions options;
        options.includeUInt16 = false;
        options.includeInt16 = false;
        options.includeUInt32 = false;
        options.includeInt32 = false;
        options.includeFloat32 = true;
        options.tolerance.absolute = 0.001;

        const svm::matching::CandidateGenerationResult candidates = svm::matching::generateValueCandidates(samples, target, options);
        QVERIFY2(candidates.success, qPrintable(candidates.errorMessage));
        QCOMPARE(candidates.candidates.size(), 1);

        const svm::matching::ValueMatchCandidate& candidate = candidates.candidates.first();
        QCOMPARE(candidate.type, svm::matching::NumericCandidateType::Float32);
        QCOMPARE(candidate.sessionId, QStringLiteral("scan-candidates"));
        QCOMPARE(candidate.slaveId, 1);
        QCOMPARE(candidate.functionCode, 0x03);
        QCOMPARE(candidate.startAddress, 100);
        QCOMPARE(candidate.registerCount, 2);
        QCOMPARE(candidate.observationIds, QList<qint64>({observations[0].id, observations[1].id}));
        QCOMPARE(candidate.addresses, QList<int>({100, 101}));
        QCOMPARE(candidate.blockIndexes, QList<int>({0, 0}));
        QCOMPARE(candidate.attemptIndexes, QList<int>({0, 0}));
        QVERIFY(candidate.absoluteError < 0.001);
        QVERIFY(candidate.evidenceText.contains(QStringLiteral("单样本候选")));
        QVERIFY(candidate.evidenceText.contains(QStringLiteral("地址 100")));
    }

    void savesExceptionAttemptWithoutDroppingErrorContext() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath(QStringLiteral("scan-errors.sqlite"));

        const auto plan = makePlan(0, 3, 2);
        const QByteArray okResponse = normalReadResponse(1, 0x03, {21, 22});
        const QByteArray exception = exceptionResponse(1, 0x03, 0x02);
        FakeModbusRtuTransport transport;
        transport.enqueue(plan.blocks[0].requestFrame, okResponse);
        transport.enqueue(plan.blocks[1].requestFrame, exception);
        svm::modbus::ModbusScanExecutor executor(transport);
        const auto result = executor.execute(plan);
        QCOMPARE(result.status, svm::modbus::ScanExecutionStatus::CompletedWithErrors);

        svm::storage::SessionStore store;
        QVERIFY2(store.open(dbPath), qPrintable(store.lastErrorText()));
        QVERIFY2(store.saveScanExecution(toPersistence(QStringLiteral("scan-exception"), result)), qPrintable(store.lastErrorText()));

        const auto session = store.scanSession(QStringLiteral("scan-exception"));
        QVERIFY(session.has_value());
        QCOMPARE(session->status, QStringLiteral("CompletedWithErrors"));
        QCOMPARE(session->successBlockCount, 1);
        QCOMPARE(session->failedBlockCount, 1);
        QVERIFY(session->errorMessage.contains(QStringLiteral("Modbus 异常")));

        const auto attempts = store.scanAttempts(QStringLiteral("scan-exception"));
        QCOMPARE(attempts.size(), 2);
        QCOMPARE(attempts[0].status, QStringLiteral("Success"));
        QCOMPARE(attempts[1].status, QStringLiteral("ModbusException"));
        QVERIFY(attempts[1].isModbusException);
        QCOMPARE(attempts[1].exceptionCode, 0x02);
        QCOMPARE(attempts[1].exceptionDescription, QStringLiteral("非法数据地址"));
        QCOMPARE(attempts[1].requestFrame, plan.blocks[1].requestFrame);
        QCOMPARE(attempts[1].responseFrame, exception);

        const auto observations = store.scanObservations(QStringLiteral("scan-exception"));
        QCOMPARE(observations.size(), 2);
        QCOMPARE(observations[0].value, 21);
        QCOMPARE(observations[1].value, 22);
    }
};

QTEST_MAIN(ModbusScanPersistenceTests)
#include "modbus_scan_persistence_tests.moc"
