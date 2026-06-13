#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

    bool appendRawEvent(const RawIoEvent& event);
    bool appendRawEvents(const std::vector<RawIoEvent>& events);
    std::int64_t rawEventCount() const;
    std::vector<RawIoEvent> recentRawEvents(std::size_t limit = 100) const;

    bool saveSendHistory(SendHistoryEntry entry, int limit = 100);
    std::vector<SendHistoryEntry> recentSendHistory(int limit = 100) const;

    bool saveSerialProfile(SerialProfile profile);
    std::optional<SerialProfile> latestSerialProfile() const;

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
    bool ensureOpen(std::string_view operation) const;
    bool appendRecord(std::string_view fileName, const Record& record);
    bool appendRecords(std::string_view fileName, const std::vector<Record>& records);
    std::vector<Record> loadRecords(std::string_view fileName) const;
    bool rewriteRecords(std::string_view fileName, const std::vector<Record>& records);
    std::filesystem::path filePath(std::string_view fileName) const;
    std::int64_t allocateId(std::string_view fileName);
    void reloadCounters();

    std::filesystem::path storeDirectory_;
    bool opened_ = false;
    mutable std::string lastErrorText_;
    std::unordered_map<std::string, std::int64_t> nextIds_;
};

} // namespace svm::native_storage
