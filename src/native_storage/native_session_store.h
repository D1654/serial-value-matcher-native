#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "storage/session_store_port.h"

namespace svm::native_storage {

struct RawIoEvent {
    std::int64_t id = 0;
    std::string sessionId;
    std::string direction;
    std::string timestampUtc;
    std::string endpoint;
    std::vector<std::uint8_t> payload;
};

struct SendHistoryEntry {
    std::int64_t id = 0;
    std::string content;
    int payloadMode = 0;
    int lineEnding = 0;
    int textEncodingCodePage = 65001;
    std::string sentAtUtc;
};

struct SerialProfile {
    std::int64_t id = 0;
    std::string name = "default";
    std::string portName;
    int baudRate = 115200;
    int dataBits = 8;
    std::string parity = "None";
    std::string stopBits = "One";
    std::string flowControl = "None";
    bool dataTerminalReady = false;
    bool requestToSend = false;
    std::string updatedAtUtc;
};

struct UiPreferences {
    std::int64_t id = 0;
    std::string name = "default";
    int logThemeIndex = 0;
    int logFormat = 0;
    int logEncodingCodePage = 65001;
    bool showLogTimestamps = true;
    int sendPayloadMode = 0;
    int sendTextEncodingCodePage = 65001;
    int sendLineEnding = 0;
    bool autoReconnect = false;
    bool timedSendEnabled = false;
    int timedSendPeriodMs = 1000;
    int fileSendDelayMs = 0;
    int logVisibleCharLimit = 350000;
    int rawEventRetentionLimitMb = 100;
    int workbenchHeight = 0;
    std::vector<std::string> quickSendSlots;
    int windowLeft = -1;
    int windowTop = -1;
    int windowWidth = 1220;
    int windowHeight = 780;
    std::string updatedAtUtc;
};

struct ScanSessionRecord {
    std::string sessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int endAddress = 0;
    int blockSize = 0;
    int requestCount = 0;
    std::string status;
    std::string startedAtUtc;
    std::string finishedAtUtc;
    int successBlockCount = 0;
    int failedBlockCount = 0;
    std::string errorMessage;
};

struct ScanAttemptRecord {
    std::int64_t id = 0;
    std::string sessionId;
    int blockIndex = -1;
    int attemptIndex = 0;
    int startAddress = 0;
    int quantity = 0;
    std::string status;
    std::vector<std::uint8_t> requestFrame;
    std::vector<std::uint8_t> responseFrame;
    std::string errorMessage;
    bool isModbusException = false;
    int exceptionCode = 0;
    std::string exceptionDescription;
    std::string sentAtUtc;
    std::string receivedAtUtc;
    std::string endpoint;
};

struct ScanObservationRecord {
    std::int64_t id = 0;
    std::string sessionId;
    int blockIndex = -1;
    int attemptIndex = 0;
    int slaveId = 0;
    int functionCode = 0;
    int address = 0;
    int value = 0;
    std::string observedAtUtc;
};

struct ScanExecutionRecord {
    ScanSessionRecord session;
    std::vector<ScanAttemptRecord> attempts;
    std::vector<ScanObservationRecord> observations;
};

struct MatchRunRecord {
    std::string runId;
    std::string sourceScanSessionId;
    std::string targetLabel;
    double targetValue = 0.0;
    std::string targetUnit;
    std::string sampledAtUtc;
    double toleranceAbsolute = 0.0;
    double toleranceRelativeRatio = 0.0;
    int candidateCount = 0;
    std::string createdAtUtc;
};

struct MatchCandidateRecord {
    std::int64_t id = 0;
    std::string runId;
    int rankIndex = 0;
    std::string candidateType;
    std::string wordOrder;
    std::string byteOrder;
    std::string sourceSessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    std::vector<std::int64_t> observationIds;
    std::vector<int> addresses;
    std::vector<int> blockIndexes;
    std::vector<int> attemptIndexes;
    std::vector<int> rawRegisters;
    double decodedValue = 0.0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    double engineeringValue = 0.0;
    double delta = 0.0;
    double absoluteError = 0.0;
    double effectiveTolerance = 0.0;
    double score = 0.0;
    std::string observedAtUtc;
    std::string evidenceText;
};

struct ProtocolFieldRuleRecord {
    std::int64_t id = 0;
    std::string ruleId;
    std::string fieldName;
    std::string sourceStabilityRunId;
    std::int64_t sourceStableCandidateId = 0;
    std::string candidateType;
    std::string wordOrder;
    std::string byteOrder;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    std::string unit;
    std::string confidenceLevel;
    double stabilityScore = 0.0;
    std::string evidenceSummary;
    std::string interpretationMap;
    std::string createdAtUtc;
};

struct RuleVerificationRunRecord {
    std::int64_t id = 0;
    std::string verificationRunId;
    std::string sourceScanSessionId;
    int ruleCount = 0;
    int verifiedCount = 0;
    int missingCount = 0;
    int unsupportedCount = 0;
    std::string createdAtUtc;
};

struct RuleVerificationResultRecord {
    std::int64_t id = 0;
    std::string verificationRunId;
    std::string ruleId;
    std::string fieldName;
    std::string unit;
    std::string candidateType;
    std::string sourceScanSessionId;
    bool verified = false;
    std::string statusText;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    std::vector<std::int64_t> observationIds;
    std::vector<int> rawRegisters;
    double decodedValue = 0.0;
    double engineeringValue = 0.0;
    std::string observedAtUtc;
    std::string interpretationText;
    std::string evidenceText;
};

class NativeSessionStore final {
public:
    using Record = std::vector<std::string>;

    bool open(const std::filesystem::path& storeDirectory);
    bool initializeSchema();
    bool isOpen() const;
    std::string lastErrorText() const;
    std::string lastRecoveryText() const;

    bool appendRawEvent(const RawIoEvent& event);
    bool appendRawEvents(const std::vector<RawIoEvent>& events);
    void setRawEventRetentionLimit(std::uintmax_t softLimitBytes, std::uintmax_t targetBytes);
    std::int64_t rawEventCount() const;
    std::vector<RawIoEvent> recentRawEvents(std::size_t limit = 100) const;

    bool saveSendHistory(SendHistoryEntry entry, int limit = 100);
    std::vector<SendHistoryEntry> recentSendHistory(int limit = 100) const;

    bool saveSerialProfile(SerialProfile profile);
    std::optional<SerialProfile> latestSerialProfile() const;

    bool saveUiPreferences(UiPreferences preferences);
    std::optional<UiPreferences> latestUiPreferences() const;

    bool saveScanExecution(const ScanExecutionRecord& execution);
    std::vector<ScanSessionRecord> recentScanSessions(int limit = 50) const;
    std::optional<ScanSessionRecord> latestScanSession() const;
    std::optional<ScanSessionRecord> scanSession(std::string_view sessionId) const;
    std::vector<ScanAttemptRecord> scanAttempts(std::string_view sessionId) const;
    std::vector<ScanObservationRecord> scanObservations(std::string_view sessionId) const;

    bool saveMatchRun(MatchRunRecord run, std::vector<MatchCandidateRecord> candidates);
    std::vector<MatchRunRecord> recentMatchRuns(int limit = 50) const;
    std::optional<MatchRunRecord> latestMatchRun() const;
    std::optional<MatchRunRecord> matchRun(std::string_view runId) const;
    std::vector<MatchCandidateRecord> matchCandidates(std::string_view runId) const;

    bool saveProtocolFieldRule(ProtocolFieldRuleRecord rule);
    bool deleteProtocolFieldRule(std::string_view ruleId);
    std::optional<ProtocolFieldRuleRecord> protocolFieldRule(std::string_view ruleId) const;
    std::vector<ProtocolFieldRuleRecord> recentProtocolFieldRules(int limit = 50) const;

    bool saveRuleVerificationRun(
        RuleVerificationRunRecord run,
        std::vector<RuleVerificationResultRecord> results);
    std::optional<RuleVerificationRunRecord> latestRuleVerificationRun() const;
    std::optional<RuleVerificationRunRecord> ruleVerificationRun(std::string_view verificationRunId) const;
    std::vector<RuleVerificationResultRecord> ruleVerificationResults(std::string_view verificationRunId) const;

private:
    struct RewriteRequest {
        std::string fileName;
        std::function<bool(const Record&)> keepRecord;
        std::vector<Record> appendedRecords;
    };

    bool ensureOpen(std::string_view operation) const;
    bool appendRecords(std::string_view fileName, const std::vector<Record>& records);
    bool compactRawEventsIfNeeded();
    bool recoverRecordFileTail(std::string_view fileName);
    bool visitRecords(std::string_view fileName, const std::function<bool(const Record&)>& visitor) const;
    std::vector<Record> loadRecords(std::string_view fileName) const;
    bool rewriteRecords(std::string_view fileName, const std::vector<Record>& records);
    bool rewriteRecordsFiltered(
        std::string_view fileName,
        const std::function<bool(const Record&)>& keepRecord,
        const std::vector<Record>& appendedRecords);
    bool rewriteRecordsFilteredTransaction(std::vector<RewriteRequest> requests);
    std::filesystem::path filePath(std::string_view fileName) const;
    void absorbRecordIds(std::string_view fileName, const std::vector<Record>& records);
    std::int64_t allocateId(std::string_view fileName);
    std::vector<Record> counterRecords() const;
    bool loadPersistedCounters();
    bool persistCounters();
    bool reloadCounters();
    void invalidateCachesForFile(std::string_view fileName) const;
    const std::vector<ScanSessionRecord>& cachedScanSessions() const;
    const std::vector<MatchRunRecord>& cachedMatchRuns() const;
    const std::vector<RuleVerificationRunRecord>& cachedRuleVerificationRuns() const;
    const std::vector<ScanAttemptRecord>& cachedScanAttempts(std::string_view sessionId) const;
    const std::vector<ScanObservationRecord>& cachedScanObservations(std::string_view sessionId) const;
    const std::vector<MatchCandidateRecord>& cachedMatchCandidates(std::string_view runId) const;
    const std::vector<RuleVerificationResultRecord>& cachedRuleVerificationResults(std::string_view verificationRunId) const;

    std::filesystem::path storeDirectory_;
    bool opened_ = false;
    mutable std::string lastErrorText_;
    std::string lastRecoveryText_;
    std::unordered_map<std::string, std::int64_t> nextIds_;
    bool countersDirty_ = false;
    std::uintmax_t rawEventSoftLimitBytes_ = 100ULL * 1024ULL * 1024ULL;
    std::uintmax_t rawEventTargetBytes_ = 80ULL * 1024ULL * 1024ULL;
    mutable bool scanSessionsCacheValid_ = false;
    mutable std::vector<ScanSessionRecord> scanSessionsCache_;
    mutable bool matchRunsCacheValid_ = false;
    mutable std::vector<MatchRunRecord> matchRunsCache_;
    mutable bool ruleVerificationRunsCacheValid_ = false;
    mutable std::vector<RuleVerificationRunRecord> ruleVerificationRunsCache_;
    mutable bool scanAttemptsCacheValid_ = false;
    mutable std::string scanAttemptsCacheSessionId_;
    mutable std::vector<ScanAttemptRecord> scanAttemptsCache_;
    mutable bool scanObservationsCacheValid_ = false;
    mutable std::string scanObservationsCacheSessionId_;
    mutable std::vector<ScanObservationRecord> scanObservationsCache_;
    mutable bool matchCandidatesCacheValid_ = false;
    mutable std::string matchCandidatesCacheRunId_;
    mutable std::vector<MatchCandidateRecord> matchCandidatesCache_;
    mutable bool ruleVerificationResultsCacheValid_ = false;
    mutable std::string ruleVerificationResultsCacheRunId_;
    mutable std::vector<RuleVerificationResultRecord> ruleVerificationResultsCache_;
};

} // namespace svm::native_storage

namespace svm::storage {

template <>
struct SessionStorePortTraits<native_storage::NativeSessionStore> {
    static constexpr SessionStorePortDescriptor descriptor{
        "native-file-session-store",
        SessionStoreBackendKind::NativeFile,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
    };

    using OpenLocation = std::filesystem::path;
    using ErrorText = std::string;
    using RawIoEvent = native_storage::RawIoEvent;
    using RawIoEventBatch = std::vector<native_storage::RawIoEvent>;
    using RawIoEventList = std::vector<native_storage::RawIoEvent>;
    using RawIoCount = std::int64_t;
    using SerialProfile = native_storage::SerialProfile;
    using UiPreferences = native_storage::UiPreferences;
    using ScanExecution = native_storage::ScanExecutionRecord;
    using ScanSessionId = std::string_view;
    using ScanSession = native_storage::ScanSessionRecord;
    using ScanSessionList = std::vector<native_storage::ScanSessionRecord>;
    using ScanAttemptList = std::vector<native_storage::ScanAttemptRecord>;
    using ScanObservationList = std::vector<native_storage::ScanObservationRecord>;
    using MatchRun = native_storage::MatchRunRecord;
    using MatchRunId = std::string_view;
    using MatchCandidatesInput = std::vector<native_storage::MatchCandidateRecord>;
    using MatchRunList = std::vector<native_storage::MatchRunRecord>;
    using MatchCandidateList = std::vector<native_storage::MatchCandidateRecord>;
    using ProtocolFieldRule = native_storage::ProtocolFieldRuleRecord;
    using RuleId = std::string_view;
    using ProtocolFieldRuleList = std::vector<native_storage::ProtocolFieldRuleRecord>;
    using RuleVerificationRun = native_storage::RuleVerificationRunRecord;
    using RuleVerificationRunId = std::string_view;
    using RuleVerificationResultsInput = std::vector<native_storage::RuleVerificationResultRecord>;
    using RuleVerificationResultList = std::vector<native_storage::RuleVerificationResultRecord>;
};

static_assert(SessionStorePort<native_storage::NativeSessionStore>);
static_assert(SessionStoreOpenStatePort<native_storage::NativeSessionStore>);
static_assert(SessionStoreRecentRawIoPort<native_storage::NativeSessionStore>);
static_assert(SessionStoreRawRetentionPort<native_storage::NativeSessionStore>);
static_assert(SessionStoreUiPreferencesPort<native_storage::NativeSessionStore>);

} // namespace svm::storage
