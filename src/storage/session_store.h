#pragma once

#include <QDateTime>
#include <QList>
#include <optional>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

#include "capture/raw_io_event.h"
#include "matching/candidate_stability_analyzer.h"
#include "matching/protocol_rule_verifier.h"
#include "matching/value_candidate_generator.h"
#include "storage/match_persistence_records.h"
#include "storage/protocol_rule_records.h"
#include "storage/rule_verification_persistence_records.h"
#include "storage/send_history_entry.h"
#include "storage/serial_profile.h"
#include "storage/scan_persistence_records.h"
#include "storage/stability_persistence_records.h"

namespace svm::storage {

class SessionStore final : public QObject {
    Q_OBJECT

public:
    explicit SessionStore(QObject* parent = nullptr);
    ~SessionStore() override;

    bool open(const QString& databasePath);
    bool initializeSchema();
    bool appendRawEvent(const capture::RawIoEvent& event);
    qint64 rawEventCount() const;
    bool saveSendHistory(QString content, protocol::PayloadMode mode, protocol::LineEnding lineEnding, int limit = 100);
    QList<SendHistoryEntry> recentSendHistory(int limit = 100) const;
    bool saveSerialProfile(const SerialProfile& profile);
    std::optional<SerialProfile> latestSerialProfile() const;
    bool saveScanExecution(const ScanExecutionPersistenceRecord& execution);
    std::optional<ScanSessionRecord> scanSession(const QString& sessionId) const;
    std::optional<ScanSessionRecord> latestScanSession() const;
    QList<ScanSessionRecord> recentScanSessions(int limit = 20) const;
    QList<ScanAttemptRecord> scanAttempts(const QString& sessionId) const;
    QList<ScanObservationRecord> scanObservations(const QString& sessionId) const;
    QList<ScanObservationRecord> scanObservationsByIds(const QList<qint64>& observationIds) const;
    bool saveMatchRun(const MatchRunRecord& run, const QList<matching::ValueMatchCandidate>& candidates);
    std::optional<MatchRunRecord> matchRun(const QString& runId) const;
    QList<MatchCandidateRecord> matchCandidates(const QString& runId) const;
    bool saveStabilityRun(const StabilityRunRecord& run, const QList<matching::StableCandidate>& candidates);
    std::optional<StabilityRunRecord> stabilityRun(const QString& stabilityRunId) const;
    std::optional<StabilityRunRecord> latestStabilityRun() const;
    QList<StableCandidateRecord> stableCandidates(const QString& stabilityRunId) const;
    bool saveProtocolFieldRule(const ProtocolFieldRuleRecord& rule);
    bool deleteProtocolFieldRule(const QString& ruleId);
    std::optional<ProtocolFieldRuleRecord> protocolFieldRule(const QString& ruleId) const;
    QList<ProtocolFieldRuleRecord> recentProtocolFieldRules(int limit = 50) const;
    bool saveRuleVerificationRun(const RuleVerificationRunRecord& run, const matching::ProtocolRuleVerificationSummary& summary);
    std::optional<RuleVerificationRunRecord> ruleVerificationRun(const QString& verificationRunId) const;
    std::optional<RuleVerificationRunRecord> latestRuleVerificationRun() const;
    QList<RuleVerificationResultRecord> ruleVerificationResults(const QString& verificationRunId) const;
    bool hasReadError() const;
    QString lastReadErrorText() const;
    void clearReadError() const;
    QString lastErrorText() const;

private:
    static QString dateToString(const QDateTime& value);
    static QString notNullString(const QString& value);
    static QDateTime dateFromString(const QString& value);
    static QString joinLongList(const QList<qint64>& values);
    static QString joinIntList(const QList<int>& values);
    static QString joinUInt16List(const QList<quint16>& values);
    static QString joinStringList(const QList<QString>& values);
    static QList<QString> parseStringList(const QString& text);
    static QList<qint64> parseLongList(const QString& text);
    static QList<int> parseIntList(const QString& text);
    static QString candidateTypeKey(matching::NumericCandidateType type);
    static QString wordOrderKey(matching::WordOrder order);
    static QString byteOrderKey(matching::ByteOrder order);

    void setReadError(const QString& operation, const QSqlError& error) const;
    void closeDatabaseConnection();

    QString m_connectionName;
    QSqlDatabase m_db;
    QString m_lastErrorText;
    mutable QString m_lastReadErrorText;
};

} // namespace svm::storage
