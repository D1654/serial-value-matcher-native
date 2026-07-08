#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "matching/scan_observation_adapter.h"
#include "matching/value_candidate_generator.h"
#include "storage/session_store.h"
#include "transport/serial_write_queue.h"

#include <cstdint>

namespace {

svm::storage::ScanExecutionPersistenceRecord persistedScan(QString sessionId, int offset)
{
    svm::storage::ScanExecutionPersistenceRecord record;
    record.session.sessionId = sessionId;
    record.session.slaveId = 1;
    record.session.functionCode = 3;
    record.session.startAddress = offset;
    record.session.endAddress = offset + 15;
    record.session.blockSize = 16;
    record.session.requestCount = 1;
    record.session.status = QStringLiteral("Completed");
    record.session.startedAtUtc = QDateTime::fromString(QStringLiteral("2026-06-12T00:00:00.000Z"), Qt::ISODateWithMs).addSecs(offset);
    record.session.finishedAtUtc = record.session.startedAtUtc.addMSecs(50);
    record.session.successBlockCount = 1;
    record.session.failedBlockCount = 0;

    for (int index = 0; index < 16; ++index) {
        svm::storage::ScanObservationRecord observation;
        observation.sessionId = sessionId;
        observation.blockIndex = 0;
        observation.attemptIndex = 0;
        observation.slaveId = 1;
        observation.functionCode = 3;
        observation.address = offset + index;
        observation.value = 1000 + index;
        observation.observedAtUtc = record.session.finishedAtUtc;
        record.observations.append(observation);
    }
    return record;
}

} // namespace

class QualityStressTests final : public QObject {
    Q_OBJECT

private slots:
    void repeatedSessionStoreOpenSaveReadCycles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString databasePath = dir.filePath(QStringLiteral("quality-stress.sqlite"));

        for (int iteration = 0; iteration < 50; ++iteration) {
            svm::storage::SessionStore store;
            QVERIFY2(store.open(databasePath), qPrintable(store.lastErrorText()));
            const QString sessionId = QStringLiteral("stress-scan-%1").arg(iteration);
            QVERIFY2(store.saveScanExecution(persistedScan(sessionId, iteration * 16)), qPrintable(store.lastErrorText()));
            const auto observations = store.scanObservations(sessionId);
            QVERIFY(!store.hasReadError());
            QCOMPARE(observations.size(), 16);

            const QList<svm::matching::RegisterSample> samples = svm::matching::registerSamplesFromScanObservations(observations);
            svm::matching::TargetValue target;
            target.label = QStringLiteral("压力目标");
            target.value = 1001.0;
            target.sampledAtUtc = QDateTime::currentDateTimeUtc();
            svm::matching::CandidateGenerationOptions options;
            options.tolerance.absolute = 1000000.0;
            options.maxCandidates = 20;
            const auto result = svm::matching::generateValueCandidates(samples, target, options);
            QVERIFY2(result.success, qPrintable(result.errorMessage));
            QVERIFY(result.candidates.size() <= options.maxCandidates);
        }
    }

    void repeatedSerialWriteQueueBurstCycles()
    {
        svm::transport::SerialWriteQueue queue(64);

        for (int cycle = 0; cycle < 500; ++cycle) {
            for (int index = 0; index < 64; ++index) {
                const auto accepted = queue.enqueue({
                    static_cast<std::uint8_t>(index),
                    static_cast<std::uint8_t>(cycle & 0xFF),
                    static_cast<std::uint8_t>((cycle >> 8) & 0xFF),
                }, 250);
                QVERIFY(accepted.status == svm::transport::SerialWriteResultStatus::Accepted);
            }
            QVERIFY(queue.full());

            for (int index = 0; index < 64; ++index) {
                const auto sent = queue.completeNextSent(3);
                QVERIFY(sent.status == svm::transport::SerialWriteResultStatus::Sent);
                QVERIFY(sent.terminal());
            }
            QVERIFY(queue.empty());
        }
    }
};

QTEST_MAIN(QualityStressTests)
#include "quality_stress_tests.moc"
