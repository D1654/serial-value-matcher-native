#include <QtTest/QtTest>

#include "capture/capture_bus.h"
#include "capture/session_evidence.h"
#include "session/console_model.h"

class SessionEvidenceTests final : public QObject {
    Q_OBJECT

private:
    static QDateTime fixedTimestamp() {
        return QDateTime::fromString(QStringLiteral("2026-07-09T06:00:00.123Z"), Qt::ISODateWithMs);
    }

private slots:
    void eventTypeKeysAreSerializationReady() {
        using svm::capture::SessionEvidenceEventType;

        const QList<SessionEvidenceEventType> types{
            SessionEvidenceEventType::RawTx,
            SessionEvidenceEventType::RawRx,
            SessionEvidenceEventType::UserCommand,
            SessionEvidenceEventType::ModbusScanSettings,
            SessionEvidenceEventType::CommandSequenceStep,
            SessionEvidenceEventType::CommandSequenceAssertion,
            SessionEvidenceEventType::DangerousOperationConfirmation,
            SessionEvidenceEventType::MatchResult,
            SessionEvidenceEventType::ReportMetadata,
            SessionEvidenceEventType::AppVersion,
        };

        for (const SessionEvidenceEventType type : types) {
            const QString key = svm::capture::sessionEvidenceEventTypeKey(type);
            QVERIFY2(!key.isEmpty(), "event type key must not be empty");
            QVERIFY2(!key.contains(QLatin1Char(' ')), "event type key must be token-like");
            const auto parsed = svm::capture::parseSessionEvidenceEventType(key);
            QVERIFY(parsed.has_value());
            QVERIFY(*parsed == type);
        }

        QVERIFY(!svm::capture::parseSessionEvidenceEventType(QStringLiteral("unknown_event")).has_value());
    }

    void rawIoConversionPreservesPayloadBeforeFormatting() {
        svm::capture::RawIoEvent raw;
        raw.sessionId = QStringLiteral("session-a");
        raw.direction = svm::capture::Direction::Tx;
        raw.timestampUtc = fixedTimestamp();
        raw.sourceSubsystem = QStringLiteral("serial_port_service");
        raw.endpoint = QStringLiteral("COM7");
        raw.payload = QByteArray::fromHex("00410d0a");

        const auto evidence = svm::capture::sessionEvidenceFromRawIoEvent(raw, 7);

        QVERIFY(evidence.type == svm::capture::SessionEvidenceEventType::RawTx);
        QCOMPARE(evidence.order, quint64{7});
        QCOMPARE(evidence.sessionId, QStringLiteral("session-a"));
        QCOMPARE(evidence.sourceSubsystem, QStringLiteral("serial_port_service"));
        QCOMPARE(evidence.endpoint, QStringLiteral("COM7"));
        QCOMPARE(evidence.rawPayload, raw.payload);
        QVERIFY(evidence.isRawIo());

        const auto direction = svm::capture::rawIoDirection(evidence.type);
        QVERIFY(direction.has_value());
        QVERIFY(*direction == svm::capture::Direction::Tx);

        const auto line = svm::session::ConsoleModel::makeLine(evidence);
        QCOMPARE(line.evidenceOrder, quint64{7});
        QCOMPARE(line.sessionId, QStringLiteral("session-a"));
        QCOMPARE(line.sourceSubsystem, QStringLiteral("serial_port_service"));
        QCOMPARE(line.hexText, QStringLiteral("00 41 0D 0A"));
        QCOMPARE(line.textPreview, QStringLiteral("\0A\\r\\n"));
    }

    void sequencerAssignsMonotonicOrderAndSessionFallback() {
        svm::capture::SessionEvidenceSequencer sequencer(QStringLiteral("fallback-session"));

        svm::capture::RawIoEvent raw;
        raw.direction = svm::capture::Direction::Rx;
        raw.timestampUtc = fixedTimestamp();
        raw.endpoint = QStringLiteral("COM8");
        raw.payload = QByteArray::fromHex("01");

        const auto first = sequencer.nextRawIoEvent(raw, QStringLiteral("serial_transport"));
        const auto second = sequencer.nextEvent(
            svm::capture::SessionEvidenceEventType::UserCommand,
            QStringLiteral("manual_send"),
            fixedTimestamp());

        QCOMPARE(first.sessionId, QStringLiteral("fallback-session"));
        QCOMPARE(first.order, quint64{1});
        QCOMPARE(second.sessionId, QStringLiteral("fallback-session"));
        QCOMPARE(second.order, quint64{2});
        QCOMPARE(sequencer.nextOrder(), quint64{3});
    }

    void metadataTracksPrivacySensitiveFields() {
        svm::capture::SessionEvidenceEvent event;
        event.type = svm::capture::SessionEvidenceEventType::UserCommand;

        event.setMetadata(
            QStringLiteral(" device_serial "),
            QStringLiteral("SN-123456"),
            svm::capture::EvidenceFieldPrivacy::Sensitive);
        event.setMetadata(QStringLiteral("operator_note"), QStringLiteral("manual smoke test"));

        QCOMPARE(event.metadata.value(QStringLiteral("device_serial")), QStringLiteral("SN-123456"));
        QCOMPARE(event.metadata.value(QStringLiteral("operator_note")), QStringLiteral("manual smoke test"));
        QVERIFY(event.isMetadataSensitive(QStringLiteral("device_serial")));
        QVERIFY(!event.isMetadataSensitive(QStringLiteral("operator_note")));

        event.setMetadata(
            QStringLiteral("device_serial"),
            QStringLiteral("redacted in export"),
            svm::capture::EvidenceFieldPrivacy::Public);

        QCOMPARE(event.metadata.value(QStringLiteral("device_serial")), QStringLiteral("redacted in export"));
        QVERIFY(!event.isMetadataSensitive(QStringLiteral("device_serial")));
    }

    void captureBusEmitsStructuredEvidenceBeforeRawEvent() {
        svm::capture::CaptureBus bus;
        QSignalSpy evidenceSpy(&bus, &svm::capture::CaptureBus::evidenceCaptured);
        QSignalSpy rawSpy(&bus, &svm::capture::CaptureBus::eventCaptured);
        QStringList signalOrder;

        connect(&bus, &svm::capture::CaptureBus::evidenceCaptured, this, [&signalOrder](const svm::capture::SessionEvidenceEvent&) {
            signalOrder.append(QStringLiteral("evidence"));
        });
        connect(&bus, &svm::capture::CaptureBus::eventCaptured, this, [&signalOrder](const svm::capture::RawIoEvent&) {
            signalOrder.append(QStringLiteral("raw"));
        });

        svm::capture::RawIoEvent raw;
        raw.sessionId = QStringLiteral("session-b");
        raw.direction = svm::capture::Direction::Rx;
        raw.timestampUtc = fixedTimestamp();
        raw.sourceSubsystem = QStringLiteral("modbus_rtu_serial_transport");
        raw.endpoint = QStringLiteral("COM9");
        raw.payload = QByteArray::fromHex("0103020001");

        bus.publish(raw);

        QCOMPARE(evidenceSpy.size(), 1);
        QCOMPARE(rawSpy.size(), 1);
        QCOMPARE(signalOrder, QStringList({QStringLiteral("evidence"), QStringLiteral("raw")}));
        QCOMPARE(bus.nextEvidenceOrder(), quint64{2});

        const auto evidence = evidenceSpy.takeFirst().at(0).value<svm::capture::SessionEvidenceEvent>();
        QVERIFY(evidence.type == svm::capture::SessionEvidenceEventType::RawRx);
        QCOMPARE(evidence.order, quint64{1});
        QCOMPARE(evidence.rawPayload, raw.payload);
        QCOMPARE(evidence.sourceSubsystem, QStringLiteral("modbus_rtu_serial_transport"));
    }
};

QTEST_MAIN(SessionEvidenceTests)
#include "session_evidence_tests.moc"
