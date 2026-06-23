#include "native_storage/native_session_store.h"
#include "native_storage/native_store_file_ops.h"
#include "native_storage/native_store_record_io.h"

#include <algorithm>
#include <charconv>
#include <deque>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace svm::native_storage {
namespace {

constexpr std::string_view kSchemaFile = "schema.txt";
constexpr std::string_view kIdCountersFile = "id_counters.svmr";
constexpr std::string_view kRawEventsFile = "raw_io_events.svmr";
constexpr std::string_view kSendHistoryFile = "send_history.svmr";
constexpr std::string_view kSerialProfilesFile = "serial_profiles.svmr";
constexpr std::string_view kUiPreferencesFile = "ui_preferences.svmr";
constexpr std::string_view kScanSessionsFile = "scan_sessions.svmr";
constexpr std::string_view kScanAttemptsFile = "scan_attempts.svmr";
constexpr std::string_view kScanObservationsFile = "scan_observations.svmr";
constexpr std::string_view kMatchRunsFile = "match_runs.svmr";
constexpr std::string_view kMatchCandidatesFile = "match_candidates.svmr";
constexpr std::string_view kProtocolRulesFile = "protocol_field_rules.svmr";
constexpr std::string_view kRuleVerificationRunsFile = "rule_verification_runs.svmr";
constexpr std::string_view kRuleVerificationResultsFile = "rule_verification_results.svmr";

const std::vector<std::string_view>& storeFiles() {
    static const std::vector<std::string_view> files = {
        kRawEventsFile,
        kSendHistoryFile,
        kSerialProfilesFile,
        kUiPreferencesFile,
        kScanSessionsFile,
        kScanAttemptsFile,
        kScanObservationsFile,
        kMatchRunsFile,
        kMatchCandidatesFile,
        kProtocolRulesFile,
        kRuleVerificationRunsFile,
        kRuleVerificationResultsFile,
    };
    return files;
}

using store_io::RecordSpan;
using store_io::copyFileTailWithHeader;
using store_io::kHeader;
using store_io::parseRecords;
using store_io::readNextRecordFromStream;
using store_io::readNextRecordSpan;
using store_io::skipStoreHeader;
using store_io::writeRecord;
using store_file_ops::ReplacementTarget;
using store_file_ops::commitReplacementTargets;
using store_file_ops::recoverReplacementArtifacts;
using store_file_ops::replaceFileWithTemp;
using store_file_ops::replacementBackupPath;
using store_file_ops::replacementTempPath;

std::string toString(std::int64_t value) {
    return std::to_string(value);
}

std::string toString(int value) {
    return std::to_string(value);
}

std::string toString(double value) {
    std::ostringstream output;
    output << std::setprecision(17) << value;
    return output.str();
}

std::string toString(bool value) {
    return value ? "1" : "0";
}

std::int64_t toInt64(const std::string& value, std::int64_t fallback = 0) {
    std::int64_t parsed = fallback;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} ? parsed : fallback;
}

int toInt(const std::string& value, int fallback = 0) {
    int parsed = fallback;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} ? parsed : fallback;
}

double toDouble(const std::string& value, double fallback = 0.0) {
    try {
        return value.empty() ? fallback : std::stod(value);
    } catch (...) {
        return fallback;
    }
}

bool toBool(const std::string& value) {
    return value == "1" || value == "true" || value == "True";
}

std::string bytesToString(const std::vector<std::uint8_t>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<std::uint8_t> stringToBytes(const std::string& value) {
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

template <typename T>
std::string joinNumbers(const std::vector<T>& values) {
    std::string output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        output.append(std::to_string(values[index]));
    }
    return output;
}

std::vector<int> parseIntList(const std::string& value) {
    std::vector<int> result;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        if (end > start) {
            result.push_back(toInt(value.substr(start, end - start)));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

std::vector<std::int64_t> parseInt64List(const std::string& value) {
    std::vector<std::int64_t> result;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        if (end > start) {
            result.push_back(toInt64(value.substr(start, end - start)));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

std::size_t safeRecentLimit(int limit) {
    return static_cast<std::size_t>(std::max(1, limit));
}

std::size_t safeRecentLimit(std::size_t limit) {
    return std::max<std::size_t>(1, limit);
}

template <typename T>
void appendBoundedRecent(std::deque<T>& values, T value, std::size_t limit) {
    values.push_back(std::move(value));
    while (values.size() > limit) {
        values.pop_front();
    }
}

template <typename T>
std::vector<T> recentLastFirstFromDeque(const std::deque<T>& values) {
    std::vector<T> result;
    result.reserve(values.size());
    for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator) {
        result.push_back(*iterator);
    }
    return result;
}

RawIoEvent rawEventFromRecord(const NativeSessionStore::Record& record) {
    RawIoEvent event;
    if (record.size() < 6) {
        return event;
    }
    event.id = toInt64(record[0]);
    event.sessionId = record[1];
    event.direction = record[2];
    event.timestampUtc = record[3];
    event.endpoint = record[4];
    event.payload = stringToBytes(record[5]);
    return event;
}

NativeSessionStore::Record recordFromRawEvent(const RawIoEvent& event) {
    return {
        toString(event.id),
        event.sessionId,
        event.direction,
        event.timestampUtc,
        event.endpoint,
        bytesToString(event.payload),
    };
}

SendHistoryEntry sendHistoryFromRecord(const NativeSessionStore::Record& record) {
    SendHistoryEntry entry;
    if (record.size() < 5) {
        return entry;
    }
    entry.id = toInt64(record[0]);
    entry.content = record[1];
    entry.payloadMode = toInt(record[2]);
    entry.lineEnding = toInt(record[3]);
    entry.sentAtUtc = record[4];
    if (record.size() >= 6) {
        entry.textEncodingCodePage = toInt(record[5]);
    }
    return entry;
}

NativeSessionStore::Record recordFromSendHistory(const SendHistoryEntry& entry) {
    return {
        toString(entry.id),
        entry.content,
        toString(entry.payloadMode),
        toString(entry.lineEnding),
        entry.sentAtUtc,
        toString(entry.textEncodingCodePage),
    };
}

SerialProfile serialProfileFromRecord(const NativeSessionStore::Record& record) {
    SerialProfile profile;
    if (record.size() < 11) {
        return profile;
    }
    profile.id = toInt64(record[0]);
    profile.name = record[1];
    profile.portName = record[2];
    profile.baudRate = toInt(record[3], 115200);
    profile.dataBits = toInt(record[4], 8);
    profile.parity = record[5];
    profile.stopBits = record[6];
    profile.flowControl = record[7];
    profile.dataTerminalReady = toBool(record[8]);
    profile.requestToSend = toBool(record[9]);
    profile.updatedAtUtc = record[10];
    return profile;
}

NativeSessionStore::Record recordFromSerialProfile(const SerialProfile& profile) {
    return {
        toString(profile.id),
        profile.name,
        profile.portName,
        toString(profile.baudRate),
        toString(profile.dataBits),
        profile.parity,
        profile.stopBits,
        profile.flowControl,
        toString(profile.dataTerminalReady),
        toString(profile.requestToSend),
        profile.updatedAtUtc,
    };
}

UiPreferences uiPreferencesFromRecord(const NativeSessionStore::Record& record) {
    UiPreferences preferences;
    if (record.size() < 13) {
        return preferences;
    }
    preferences.id = toInt64(record[0]);
    preferences.name = record[1];
    preferences.logThemeIndex = toInt(record[2]);
    preferences.logFormat = toInt(record[3]);
    preferences.logEncodingCodePage = toInt(record[4], 65001);
    preferences.showLogTimestamps = toBool(record[5]);
    preferences.sendPayloadMode = toInt(record[6]);
    preferences.sendTextEncodingCodePage = toInt(record[7], 65001);
    preferences.sendLineEnding = toInt(record[8]);
    preferences.windowLeft = toInt(record[9], -1);
    preferences.windowTop = toInt(record[10], -1);
    preferences.windowWidth = toInt(record[11], 1220);
    preferences.windowHeight = toInt(record[12], 780);
    if (record.size() >= 14) {
        preferences.updatedAtUtc = record[13];
    }
    if (record.size() >= 19) {
        preferences.autoReconnect = toBool(record[14]);
        preferences.timedSendEnabled = toBool(record[15]);
        preferences.timedSendPeriodMs = toInt(record[16], 1000);
        preferences.fileSendDelayMs = toInt(record[17]);
        preferences.logVisibleCharLimit = toInt(record[18], 350000);
    }
    if (record.size() >= 29) {
        preferences.quickSendSlots.clear();
        preferences.quickSendSlots.reserve(10);
        for (std::size_t index = 19; index < 29; ++index) {
            preferences.quickSendSlots.push_back(record[index]);
        }
    }
    if (record.size() >= 30) {
        preferences.rawEventRetentionLimitMb = toInt(record[29], 100);
    }
    return preferences;
}

NativeSessionStore::Record recordFromUiPreferences(const UiPreferences& preferences) {
    NativeSessionStore::Record record = {
        toString(preferences.id),
        preferences.name,
        toString(preferences.logThemeIndex),
        toString(preferences.logFormat),
        toString(preferences.logEncodingCodePage),
        toString(preferences.showLogTimestamps),
        toString(preferences.sendPayloadMode),
        toString(preferences.sendTextEncodingCodePage),
        toString(preferences.sendLineEnding),
        toString(preferences.windowLeft),
        toString(preferences.windowTop),
        toString(preferences.windowWidth),
        toString(preferences.windowHeight),
        preferences.updatedAtUtc,
    };
    record.push_back(toString(preferences.autoReconnect));
    record.push_back(toString(preferences.timedSendEnabled));
    record.push_back(toString(preferences.timedSendPeriodMs));
    record.push_back(toString(preferences.fileSendDelayMs));
    record.push_back(toString(preferences.logVisibleCharLimit));
    for (std::size_t index = 0; index < 10; ++index) {
        record.push_back(index < preferences.quickSendSlots.size() ? preferences.quickSendSlots[index] : std::string{});
    }
    record.push_back(toString(preferences.rawEventRetentionLimitMb));
    return record;
}

ScanSessionRecord scanSessionFromRecord(const NativeSessionStore::Record& record) {
    ScanSessionRecord session;
    if (record.size() < 13) {
        return session;
    }
    session.sessionId = record[0];
    session.slaveId = toInt(record[1]);
    session.functionCode = toInt(record[2]);
    session.startAddress = toInt(record[3]);
    session.endAddress = toInt(record[4]);
    session.blockSize = toInt(record[5]);
    session.requestCount = toInt(record[6]);
    session.status = record[7];
    session.startedAtUtc = record[8];
    session.finishedAtUtc = record[9];
    session.successBlockCount = toInt(record[10]);
    session.failedBlockCount = toInt(record[11]);
    session.errorMessage = record[12];
    return session;
}

NativeSessionStore::Record recordFromScanSession(const ScanSessionRecord& session) {
    return {
        session.sessionId,
        toString(session.slaveId),
        toString(session.functionCode),
        toString(session.startAddress),
        toString(session.endAddress),
        toString(session.blockSize),
        toString(session.requestCount),
        session.status,
        session.startedAtUtc,
        session.finishedAtUtc,
        toString(session.successBlockCount),
        toString(session.failedBlockCount),
        session.errorMessage,
    };
}

ScanAttemptRecord scanAttemptFromRecord(const NativeSessionStore::Record& record) {
    ScanAttemptRecord attempt;
    if (record.size() < 16) {
        return attempt;
    }
    attempt.id = toInt64(record[0]);
    attempt.sessionId = record[1];
    attempt.blockIndex = toInt(record[2]);
    attempt.attemptIndex = toInt(record[3]);
    attempt.startAddress = toInt(record[4]);
    attempt.quantity = toInt(record[5]);
    attempt.status = record[6];
    attempt.requestFrame = stringToBytes(record[7]);
    attempt.responseFrame = stringToBytes(record[8]);
    attempt.errorMessage = record[9];
    attempt.isModbusException = toBool(record[10]);
    attempt.exceptionCode = toInt(record[11]);
    attempt.exceptionDescription = record[12];
    attempt.sentAtUtc = record[13];
    attempt.receivedAtUtc = record[14];
    attempt.endpoint = record[15];
    return attempt;
}

NativeSessionStore::Record recordFromScanAttempt(const ScanAttemptRecord& attempt) {
    return {
        toString(attempt.id),
        attempt.sessionId,
        toString(attempt.blockIndex),
        toString(attempt.attemptIndex),
        toString(attempt.startAddress),
        toString(attempt.quantity),
        attempt.status,
        bytesToString(attempt.requestFrame),
        bytesToString(attempt.responseFrame),
        attempt.errorMessage,
        toString(attempt.isModbusException),
        toString(attempt.exceptionCode),
        attempt.exceptionDescription,
        attempt.sentAtUtc,
        attempt.receivedAtUtc,
        attempt.endpoint,
    };
}

ScanObservationRecord scanObservationFromRecord(const NativeSessionStore::Record& record) {
    ScanObservationRecord observation;
    if (record.size() < 9) {
        return observation;
    }
    observation.id = toInt64(record[0]);
    observation.sessionId = record[1];
    observation.blockIndex = toInt(record[2]);
    observation.attemptIndex = toInt(record[3]);
    observation.slaveId = toInt(record[4]);
    observation.functionCode = toInt(record[5]);
    observation.address = toInt(record[6]);
    observation.value = toInt(record[7]);
    observation.observedAtUtc = record[8];
    return observation;
}

NativeSessionStore::Record recordFromScanObservation(const ScanObservationRecord& observation) {
    return {
        toString(observation.id),
        observation.sessionId,
        toString(observation.blockIndex),
        toString(observation.attemptIndex),
        toString(observation.slaveId),
        toString(observation.functionCode),
        toString(observation.address),
        toString(observation.value),
        observation.observedAtUtc,
    };
}

MatchRunRecord matchRunFromRecord(const NativeSessionStore::Record& record) {
    MatchRunRecord run;
    if (record.size() < 10) {
        return run;
    }
    run.runId = record[0];
    run.sourceScanSessionId = record[1];
    run.targetLabel = record[2];
    run.targetValue = toDouble(record[3]);
    run.targetUnit = record[4];
    run.sampledAtUtc = record[5];
    run.toleranceAbsolute = toDouble(record[6]);
    run.toleranceRelativeRatio = toDouble(record[7]);
    run.candidateCount = toInt(record[8]);
    run.createdAtUtc = record[9];
    return run;
}

NativeSessionStore::Record recordFromMatchRun(const MatchRunRecord& run) {
    return {
        run.runId,
        run.sourceScanSessionId,
        run.targetLabel,
        toString(run.targetValue),
        run.targetUnit,
        run.sampledAtUtc,
        toString(run.toleranceAbsolute),
        toString(run.toleranceRelativeRatio),
        toString(run.candidateCount),
        run.createdAtUtc,
    };
}

MatchCandidateRecord matchCandidateFromRecord(const NativeSessionStore::Record& record) {
    MatchCandidateRecord candidate;
    if (record.size() < 26) {
        return candidate;
    }
    candidate.id = toInt64(record[0]);
    candidate.runId = record[1];
    candidate.rankIndex = toInt(record[2]);
    candidate.candidateType = record[3];
    candidate.wordOrder = record[4];
    candidate.byteOrder = record[5];
    candidate.sourceSessionId = record[6];
    candidate.slaveId = toInt(record[7]);
    candidate.functionCode = toInt(record[8]);
    candidate.startAddress = toInt(record[9]);
    candidate.registerCount = toInt(record[10]);
    candidate.observationIds = parseInt64List(record[11]);
    candidate.addresses = parseIntList(record[12]);
    candidate.blockIndexes = parseIntList(record[13]);
    candidate.attemptIndexes = parseIntList(record[14]);
    candidate.rawRegisters = parseIntList(record[15]);
    candidate.decodedValue = toDouble(record[16]);
    candidate.scaleMultiplier = toDouble(record[17], 1.0);
    candidate.scaleOffset = toDouble(record[18]);
    candidate.engineeringValue = toDouble(record[19]);
    candidate.delta = toDouble(record[20]);
    candidate.absoluteError = toDouble(record[21]);
    candidate.effectiveTolerance = toDouble(record[22]);
    candidate.score = toDouble(record[23]);
    candidate.observedAtUtc = record[24];
    candidate.evidenceText = record[25];
    return candidate;
}

NativeSessionStore::Record recordFromMatchCandidate(const MatchCandidateRecord& candidate) {
    return {
        toString(candidate.id),
        candidate.runId,
        toString(candidate.rankIndex),
        candidate.candidateType,
        candidate.wordOrder,
        candidate.byteOrder,
        candidate.sourceSessionId,
        toString(candidate.slaveId),
        toString(candidate.functionCode),
        toString(candidate.startAddress),
        toString(candidate.registerCount),
        joinNumbers(candidate.observationIds),
        joinNumbers(candidate.addresses),
        joinNumbers(candidate.blockIndexes),
        joinNumbers(candidate.attemptIndexes),
        joinNumbers(candidate.rawRegisters),
        toString(candidate.decodedValue),
        toString(candidate.scaleMultiplier),
        toString(candidate.scaleOffset),
        toString(candidate.engineeringValue),
        toString(candidate.delta),
        toString(candidate.absoluteError),
        toString(candidate.effectiveTolerance),
        toString(candidate.score),
        candidate.observedAtUtc,
        candidate.evidenceText,
    };
}

ProtocolFieldRuleRecord protocolRuleFromRecord(const NativeSessionStore::Record& record) {
    ProtocolFieldRuleRecord rule;
    if (record.size() < 20) {
        return rule;
    }
    rule.id = toInt64(record[0]);
    rule.ruleId = record[1];
    rule.fieldName = record[2];
    rule.sourceStabilityRunId = record[3];
    rule.sourceStableCandidateId = toInt64(record[4]);
    rule.candidateType = record[5];
    rule.wordOrder = record[6];
    rule.byteOrder = record[7];
    rule.slaveId = toInt(record[8]);
    rule.functionCode = toInt(record[9]);
    rule.startAddress = toInt(record[10]);
    rule.registerCount = toInt(record[11]);
    rule.scaleMultiplier = toDouble(record[12], 1.0);
    rule.scaleOffset = toDouble(record[13]);
    rule.unit = record[14];
    rule.confidenceLevel = record[15];
    rule.stabilityScore = toDouble(record[16]);
    rule.evidenceSummary = record[17];
    rule.interpretationMap = record[18];
    rule.createdAtUtc = record[19];
    return rule;
}

NativeSessionStore::Record recordFromProtocolRule(const ProtocolFieldRuleRecord& rule) {
    return {
        toString(rule.id),
        rule.ruleId,
        rule.fieldName,
        rule.sourceStabilityRunId,
        toString(rule.sourceStableCandidateId),
        rule.candidateType,
        rule.wordOrder,
        rule.byteOrder,
        toString(rule.slaveId),
        toString(rule.functionCode),
        toString(rule.startAddress),
        toString(rule.registerCount),
        toString(rule.scaleMultiplier),
        toString(rule.scaleOffset),
        rule.unit,
        rule.confidenceLevel,
        toString(rule.stabilityScore),
        rule.evidenceSummary,
        rule.interpretationMap,
        rule.createdAtUtc,
    };
}

RuleVerificationRunRecord verificationRunFromRecord(const NativeSessionStore::Record& record) {
    RuleVerificationRunRecord run;
    if (record.size() < 8) {
        return run;
    }
    run.id = toInt64(record[0]);
    run.verificationRunId = record[1];
    run.sourceScanSessionId = record[2];
    run.ruleCount = toInt(record[3]);
    run.verifiedCount = toInt(record[4]);
    run.missingCount = toInt(record[5]);
    run.unsupportedCount = toInt(record[6]);
    run.createdAtUtc = record[7];
    return run;
}

NativeSessionStore::Record recordFromVerificationRun(const RuleVerificationRunRecord& run) {
    return {
        toString(run.id),
        run.verificationRunId,
        run.sourceScanSessionId,
        toString(run.ruleCount),
        toString(run.verifiedCount),
        toString(run.missingCount),
        toString(run.unsupportedCount),
        run.createdAtUtc,
    };
}

RuleVerificationResultRecord verificationResultFromRecord(const NativeSessionStore::Record& record) {
    RuleVerificationResultRecord result;
    if (record.size() < 20) {
        return result;
    }
    result.id = toInt64(record[0]);
    result.verificationRunId = record[1];
    result.ruleId = record[2];
    result.fieldName = record[3];
    result.unit = record[4];
    result.candidateType = record[5];
    result.sourceScanSessionId = record[6];
    result.verified = toBool(record[7]);
    result.statusText = record[8];
    result.slaveId = toInt(record[9]);
    result.functionCode = toInt(record[10]);
    result.startAddress = toInt(record[11]);
    result.registerCount = toInt(record[12]);
    result.observationIds = parseInt64List(record[13]);
    result.rawRegisters = parseIntList(record[14]);
    result.decodedValue = toDouble(record[15]);
    result.engineeringValue = toDouble(record[16]);
    result.observedAtUtc = record[17];
    result.interpretationText = record[18];
    result.evidenceText = record[19];
    return result;
}

NativeSessionStore::Record recordFromVerificationResult(const RuleVerificationResultRecord& result) {
    return {
        toString(result.id),
        result.verificationRunId,
        result.ruleId,
        result.fieldName,
        result.unit,
        result.candidateType,
        result.sourceScanSessionId,
        toString(result.verified),
        result.statusText,
        toString(result.slaveId),
        toString(result.functionCode),
        toString(result.startAddress),
        toString(result.registerCount),
        joinNumbers(result.observationIds),
        joinNumbers(result.rawRegisters),
        toString(result.decodedValue),
        toString(result.engineeringValue),
        result.observedAtUtc,
        result.interpretationText,
        result.evidenceText,
    };
}

} // namespace

bool NativeSessionStore::open(const std::filesystem::path& storeDirectory) {
    scanSessionsCacheValid_ = false;
    matchRunsCacheValid_ = false;
    ruleVerificationRunsCacheValid_ = false;
    scanAttemptsCacheValid_ = false;
    scanObservationsCacheValid_ = false;
    matchCandidatesCacheValid_ = false;
    ruleVerificationResultsCacheValid_ = false;
    scanSessionsCache_.clear();
    matchRunsCache_.clear();
    ruleVerificationRunsCache_.clear();
    scanAttemptsCache_.clear();
    scanObservationsCache_.clear();
    matchCandidatesCache_.clear();
    ruleVerificationResultsCache_.clear();
    storeDirectory_ = storeDirectory;
    opened_ = true;
    if (!initializeSchema()) {
        opened_ = false;
        return false;
    }
    lastErrorText_.clear();
    return true;
}

bool NativeSessionStore::initializeSchema() {
    if (storeDirectory_.empty()) {
        lastErrorText_ = "初始化 native 存储失败：存储目录为空。";
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(storeDirectory_, error);
    if (error) {
        lastErrorText_ = "初始化 native 存储失败：无法创建目录 " + storeDirectory_.string() + "：" + error.message();
        return false;
    }

    {
        std::ofstream schema(filePath(kSchemaFile), std::ios::binary | std::ios::trunc);
        if (!schema) {
            lastErrorText_ = "初始化 native 存储失败：无法写入 schema.txt。";
            return false;
        }
        schema << "format=SVM_NATIVE_STORE_V1\n";
        schema << "engine=length-prefixed-files\n";
        schema << "storage_choice=file-store-first-stage\n";
        schema << "id_counters=id_counters.svmr\n";
    }

    for (std::string_view fileName : storeFiles()) {
        const auto path = filePath(fileName);
        if (!recoverReplacementArtifacts(path, lastErrorText_)) {
            return false;
        }
        if (std::filesystem::exists(path, error)) {
            continue;
        }
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            lastErrorText_ = "初始化 native 存储失败：无法创建 " + path.filename().string() + "。";
            return false;
        }
        output << kHeader;
    }

    if (!loadPersistedCounters() && !reloadCounters()) {
        return false;
    }
    lastErrorText_.clear();
    return true;
}

bool NativeSessionStore::isOpen() const {
    return opened_;
}

std::string NativeSessionStore::lastErrorText() const {
    return lastErrorText_;
}

bool NativeSessionStore::appendRawEvent(const RawIoEvent& event) {
    return appendRawEvents({event});
}

bool NativeSessionStore::appendRawEvents(const std::vector<RawIoEvent>& events) {
    if (!ensureOpen("保存串口原始事件")) {
        return false;
    }
    std::vector<Record> records;
    records.reserve(events.size());
    for (RawIoEvent event : events) {
        if (event.id <= 0) {
            event.id = allocateId(kRawEventsFile);
        }
        records.push_back(recordFromRawEvent(event));
    }
    return appendRecords(kRawEventsFile, records) && compactRawEventsIfNeeded();
}

void NativeSessionStore::setRawEventRetentionLimit(std::uintmax_t softLimitBytes, std::uintmax_t targetBytes) {
    rawEventSoftLimitBytes_ = softLimitBytes;
    rawEventTargetBytes_ = std::min(targetBytes, softLimitBytes);
}

std::int64_t NativeSessionStore::rawEventCount() const {
    std::int64_t count = 0;
    const bool ok = visitRecords(kRawEventsFile, [&](const Record&) {
        ++count;
        return true;
    });
    return ok ? count : 0;
}

std::vector<RawIoEvent> NativeSessionStore::recentRawEvents(std::size_t limit) const {
    std::deque<RawIoEvent> events;
    const std::size_t safeLimit = safeRecentLimit(limit);
    const bool ok = visitRecords(kRawEventsFile, [&](const Record& record) {
        appendBoundedRecent(events, rawEventFromRecord(record), safeLimit);
        return true;
    });
    return ok ? recentLastFirstFromDeque(events) : std::vector<RawIoEvent>{};
}

bool NativeSessionStore::saveSendHistory(SendHistoryEntry entry, int limit) {
    if (!ensureOpen("保存发送历史")) {
        return false;
    }
    if (entry.id <= 0) {
        entry.id = allocateId(kSendHistoryFile);
    }

    const std::size_t safeLimit = safeRecentLimit(limit);
    const std::size_t existingLimit = safeLimit > 0 ? safeLimit - 1 : 0;
    std::deque<Record> retainedRecords;
    const bool ok = visitRecords(kSendHistoryFile, [&](const Record& record) {
        SendHistoryEntry existing = sendHistoryFromRecord(record);
        if (existing.content == entry.content
            && existing.payloadMode == entry.payloadMode
            && existing.lineEnding == entry.lineEnding
            && existing.textEncodingCodePage == entry.textEncodingCodePage) {
            return true;
        }
        if (existingLimit > 0) {
            appendBoundedRecent(retainedRecords, record, existingLimit);
        }
        return true;
    });
    if (!ok) {
        return false;
    }

    std::vector<Record> records;
    records.reserve(retainedRecords.size() + 1);
    for (const Record& record : retainedRecords) {
        records.push_back(record);
    }
    records.push_back(recordFromSendHistory(entry));
    return rewriteRecords(kSendHistoryFile, records);
}

std::vector<SendHistoryEntry> NativeSessionStore::recentSendHistory(int limit) const {
    std::deque<SendHistoryEntry> entries;
    const std::size_t safeLimit = safeRecentLimit(limit);
    const bool ok = visitRecords(kSendHistoryFile, [&](const Record& record) {
        appendBoundedRecent(entries, sendHistoryFromRecord(record), safeLimit);
        return true;
    });
    return ok ? recentLastFirstFromDeque(entries) : std::vector<SendHistoryEntry>{};
}

bool NativeSessionStore::saveSerialProfile(SerialProfile profile) {
    if (!ensureOpen("保存串口配置")) {
        return false;
    }
    if (profile.name.empty()) {
        profile.name = "default";
    }
    if (profile.id <= 0) {
        profile.id = allocateId(kSerialProfilesFile);
    }

    const std::vector<Record> profileRecord = {recordFromSerialProfile(profile)};
    return rewriteRecordsFiltered(kSerialProfilesFile,
        [&](const Record& record) { return serialProfileFromRecord(record).name != profile.name; },
        profileRecord);
}

std::optional<SerialProfile> NativeSessionStore::latestSerialProfile() const {
    std::optional<SerialProfile> latest;
    const bool ok = visitRecords(kSerialProfilesFile, [&](const Record& record) {
        latest = serialProfileFromRecord(record);
        return true;
    });
    return ok ? latest : std::nullopt;
}

bool NativeSessionStore::saveUiPreferences(UiPreferences preferences) {
    if (!ensureOpen("保存界面偏好")) {
        return false;
    }
    if (preferences.name.empty()) {
        preferences.name = "default";
    }
    if (preferences.id <= 0) {
        preferences.id = allocateId(kUiPreferencesFile);
    }

    const std::vector<Record> preferenceRecord = {recordFromUiPreferences(preferences)};
    return rewriteRecordsFiltered(kUiPreferencesFile,
        [&](const Record& record) { return uiPreferencesFromRecord(record).name != preferences.name; },
        preferenceRecord);
}

std::optional<UiPreferences> NativeSessionStore::latestUiPreferences() const {
    std::optional<UiPreferences> latest;
    const bool ok = visitRecords(kUiPreferencesFile, [&](const Record& record) {
        latest = uiPreferencesFromRecord(record);
        return true;
    });
    return ok ? latest : std::nullopt;
}

bool NativeSessionStore::saveScanExecution(const ScanExecutionRecord& execution) {
    if (!ensureOpen("保存扫描结果")) {
        return false;
    }
    if (execution.session.sessionId.empty()) {
        lastErrorText_ = "保存扫描结果失败：扫描会话 ID 不能为空。";
        return false;
    }

    bool replacesExistingSession = false;
    if (!visitRecords(kScanSessionsFile, [&](const Record& record) {
        if (scanSessionFromRecord(record).sessionId == execution.session.sessionId) {
            replacesExistingSession = true;
            return false;
        }
        return true;
    })) {
        return false;
    }

    std::vector<Record> attempts;
    attempts.reserve(execution.attempts.size());
    for (ScanAttemptRecord attempt : execution.attempts) {
        if (attempt.sessionId.empty()) {
            attempt.sessionId = execution.session.sessionId;
        }
        if (attempt.id <= 0) {
            attempt.id = allocateId(kScanAttemptsFile);
        }
        attempts.push_back(recordFromScanAttempt(attempt));
    }

    std::vector<Record> observations;
    observations.reserve(execution.observations.size());
    for (ScanObservationRecord observation : execution.observations) {
        if (observation.sessionId.empty()) {
            observation.sessionId = execution.session.sessionId;
        }
        if (observation.id <= 0) {
            observation.id = allocateId(kScanObservationsFile);
        }
        observations.push_back(recordFromScanObservation(observation));
    }

    const std::vector<Record> sessionRecord = {recordFromScanSession(execution.session)};
    if (!replacesExistingSession) {
        return appendRecords(kScanAttemptsFile, attempts)
            && appendRecords(kScanObservationsFile, observations)
            && appendRecords(kScanSessionsFile, sessionRecord);
    }

    const std::string sessionId = execution.session.sessionId;
    return rewriteRecordsFilteredTransaction({
        RewriteRequest{
            std::string(kScanSessionsFile),
            [sessionId](const Record& record) { return scanSessionFromRecord(record).sessionId != sessionId; },
            sessionRecord,
        },
        RewriteRequest{
            std::string(kScanAttemptsFile),
            [sessionId](const Record& record) { return scanAttemptFromRecord(record).sessionId != sessionId; },
            attempts,
        },
        RewriteRequest{
            std::string(kScanObservationsFile),
            [sessionId](const Record& record) { return scanObservationFromRecord(record).sessionId != sessionId; },
            observations,
        },
    });
}

std::vector<ScanSessionRecord> NativeSessionStore::recentScanSessions(int limit) const {
    const auto& sessions = cachedScanSessions();
    const std::size_t safeLimit = safeRecentLimit(limit);
    std::vector<ScanSessionRecord> result;
    const std::size_t count = std::min(safeLimit, sessions.size());
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(sessions[sessions.size() - index - 1]);
    }
    return result;
}

std::optional<ScanSessionRecord> NativeSessionStore::latestScanSession() const {
    const auto& sessions = cachedScanSessions();
    if (sessions.empty()) {
        return std::nullopt;
    }
    return sessions.back();
}

std::optional<ScanSessionRecord> NativeSessionStore::scanSession(std::string_view sessionId) const {
    for (const ScanSessionRecord& session : cachedScanSessions()) {
        if (session.sessionId == sessionId) {
            return session;
        }
    }
    return std::nullopt;
}

std::vector<ScanAttemptRecord> NativeSessionStore::scanAttempts(std::string_view sessionId) const {
    if (sessionId.empty() || !scanSession(sessionId).has_value()) {
        return {};
    }
    return cachedScanAttempts(sessionId);
}

std::vector<ScanObservationRecord> NativeSessionStore::scanObservations(std::string_view sessionId) const {
    if (sessionId.empty() || !scanSession(sessionId).has_value()) {
        return {};
    }
    return cachedScanObservations(sessionId);
}

bool NativeSessionStore::saveMatchRun(MatchRunRecord run, std::vector<MatchCandidateRecord> candidates) {
    if (!ensureOpen("保存匹配候选")) {
        return false;
    }
    if (run.runId.empty()) {
        lastErrorText_ = "保存匹配候选失败：匹配运行 ID 不能为空。";
        return false;
    }
    run.candidateCount = static_cast<int>(candidates.size());

    bool replacesExistingRun = false;
    if (!visitRecords(kMatchRunsFile, [&](const Record& record) {
        if (matchRunFromRecord(record).runId == run.runId) {
            replacesExistingRun = true;
            return false;
        }
        return true;
    })) {
        return false;
    }

    std::vector<Record> candidateRecords;
    candidateRecords.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        MatchCandidateRecord candidate = candidates[index];
        if (candidate.runId.empty()) {
            candidate.runId = run.runId;
        }
        candidate.rankIndex = static_cast<int>(index);
        if (candidate.id <= 0) {
            candidate.id = allocateId(kMatchCandidatesFile);
        }
        candidateRecords.push_back(recordFromMatchCandidate(candidate));
    }

    const std::vector<Record> runRecord = {recordFromMatchRun(run)};
    if (!replacesExistingRun) {
        return appendRecords(kMatchCandidatesFile, candidateRecords)
            && appendRecords(kMatchRunsFile, runRecord);
    }

    const std::string runId = run.runId;
    return rewriteRecordsFilteredTransaction({
        RewriteRequest{
            std::string(kMatchRunsFile),
            [runId](const Record& record) { return matchRunFromRecord(record).runId != runId; },
            runRecord,
        },
        RewriteRequest{
            std::string(kMatchCandidatesFile),
            [runId](const Record& record) { return matchCandidateFromRecord(record).runId != runId; },
            candidateRecords,
        },
    });
}

std::vector<MatchRunRecord> NativeSessionStore::recentMatchRuns(int limit) const {
    const auto& runs = cachedMatchRuns();
    const std::size_t safeLimit = safeRecentLimit(limit);
    std::vector<MatchRunRecord> result;
    const std::size_t count = std::min(safeLimit, runs.size());
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(runs[runs.size() - index - 1]);
    }
    return result;
}

std::optional<MatchRunRecord> NativeSessionStore::latestMatchRun() const {
    const auto& runs = cachedMatchRuns();
    if (runs.empty()) {
        return std::nullopt;
    }
    return runs.back();
}

std::optional<MatchRunRecord> NativeSessionStore::matchRun(std::string_view runId) const {
    for (const MatchRunRecord& run : cachedMatchRuns()) {
        if (run.runId == runId) {
            return run;
        }
    }
    return std::nullopt;
}

std::vector<MatchCandidateRecord> NativeSessionStore::matchCandidates(std::string_view runId) const {
    if (runId.empty() || !matchRun(runId).has_value()) {
        return {};
    }
    return cachedMatchCandidates(runId);
}

bool NativeSessionStore::saveProtocolFieldRule(ProtocolFieldRuleRecord rule) {
    if (!ensureOpen("保存协议字段规则")) {
        return false;
    }
    if (rule.ruleId.empty()) {
        lastErrorText_ = "保存协议字段规则失败：规则 ID 不能为空。";
        return false;
    }
    if (rule.fieldName.empty()) {
        lastErrorText_ = "保存协议字段规则失败：字段名称不能为空。";
        return false;
    }
    if (rule.id <= 0) {
        rule.id = allocateId(kProtocolRulesFile);
    }

    const std::vector<Record> ruleRecord = {recordFromProtocolRule(rule)};
    return rewriteRecordsFiltered(kProtocolRulesFile,
        [&](const Record& record) { return protocolRuleFromRecord(record).ruleId != rule.ruleId; },
        ruleRecord);
}

bool NativeSessionStore::deleteProtocolFieldRule(std::string_view ruleId) {
    if (!ensureOpen("删除协议字段规则")) {
        return false;
    }
    return rewriteRecordsFiltered(kProtocolRulesFile,
        [&](const Record& record) { return protocolRuleFromRecord(record).ruleId != ruleId; },
        {});
}

std::optional<ProtocolFieldRuleRecord> NativeSessionStore::protocolFieldRule(std::string_view ruleId) const {
    std::optional<ProtocolFieldRuleRecord> found;
    const bool ok = visitRecords(kProtocolRulesFile, [&](const Record& record) {
        ProtocolFieldRuleRecord rule = protocolRuleFromRecord(record);
        if (rule.ruleId == ruleId) {
            found = std::move(rule);
        }
        return true;
    });
    return ok ? found : std::nullopt;
}

std::vector<ProtocolFieldRuleRecord> NativeSessionStore::recentProtocolFieldRules(int limit) const {
    std::deque<ProtocolFieldRuleRecord> rules;
    const std::size_t safeLimit = safeRecentLimit(limit);
    const bool ok = visitRecords(kProtocolRulesFile, [&](const Record& record) {
        appendBoundedRecent(rules, protocolRuleFromRecord(record), safeLimit);
        return true;
    });
    return ok ? recentLastFirstFromDeque(rules) : std::vector<ProtocolFieldRuleRecord>{};
}

bool NativeSessionStore::saveRuleVerificationRun(
    RuleVerificationRunRecord run,
    std::vector<RuleVerificationResultRecord> results) {
    if (!ensureOpen("保存规则验证结果")) {
        return false;
    }
    if (run.verificationRunId.empty()) {
        lastErrorText_ = "保存规则验证结果失败：验证运行 ID 不能为空。";
        return false;
    }
    if (run.id <= 0) {
        run.id = allocateId(kRuleVerificationRunsFile);
    }

    bool replacesExistingRun = false;
    if (!visitRecords(kRuleVerificationRunsFile, [&](const Record& record) {
        if (verificationRunFromRecord(record).verificationRunId == run.verificationRunId) {
            replacesExistingRun = true;
            return false;
        }
        return true;
    })) {
        return false;
    }

    std::vector<Record> resultRecords;
    resultRecords.reserve(results.size());
    for (RuleVerificationResultRecord result : results) {
        if (result.verificationRunId.empty()) {
            result.verificationRunId = run.verificationRunId;
        }
        if (result.id <= 0) {
            result.id = allocateId(kRuleVerificationResultsFile);
        }
        resultRecords.push_back(recordFromVerificationResult(result));
    }

    const std::vector<Record> runRecord = {recordFromVerificationRun(run)};
    if (!replacesExistingRun) {
        return appendRecords(kRuleVerificationResultsFile, resultRecords)
            && appendRecords(kRuleVerificationRunsFile, runRecord);
    }

    const std::string verificationRunId = run.verificationRunId;
    return rewriteRecordsFilteredTransaction({
        RewriteRequest{
            std::string(kRuleVerificationRunsFile),
            [verificationRunId](const Record& record) {
                return verificationRunFromRecord(record).verificationRunId != verificationRunId;
            },
            runRecord,
        },
        RewriteRequest{
            std::string(kRuleVerificationResultsFile),
            [verificationRunId](const Record& record) {
                return verificationResultFromRecord(record).verificationRunId != verificationRunId;
            },
            resultRecords,
        },
    });
}

std::optional<RuleVerificationRunRecord> NativeSessionStore::latestRuleVerificationRun() const {
    const auto& runs = cachedRuleVerificationRuns();
    if (runs.empty()) {
        return std::nullopt;
    }
    return runs.back();
}

std::optional<RuleVerificationRunRecord> NativeSessionStore::ruleVerificationRun(std::string_view verificationRunId) const {
    for (const RuleVerificationRunRecord& run : cachedRuleVerificationRuns()) {
        if (run.verificationRunId == verificationRunId) {
            return run;
        }
    }
    return std::nullopt;
}

std::vector<RuleVerificationResultRecord> NativeSessionStore::ruleVerificationResults(std::string_view verificationRunId) const {
    if (verificationRunId.empty() || !ruleVerificationRun(verificationRunId).has_value()) {
        return {};
    }
    return cachedRuleVerificationResults(verificationRunId);
}

bool NativeSessionStore::ensureOpen(std::string_view operation) const {
    if (opened_) {
        return true;
    }
    lastErrorText_ = std::string(operation) + "失败：native 存储尚未打开。";
    return false;
}

bool NativeSessionStore::appendRecords(std::string_view fileName, const std::vector<Record>& records) {
    if (records.empty()) {
        return true;
    }
    absorbRecordIds(fileName, records);
    if (fileName != kIdCountersFile && !persistCounters()) {
        return false;
    }
    const auto path = filePath(fileName);
    std::error_code error;
    const bool needsHeader = !std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0;
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        lastErrorText_ = "写入 native 存储失败：无法打开 " + path.filename().string() + "。";
        return false;
    }
    if (needsHeader) {
        output << kHeader;
    }
    for (const Record& record : records) {
        if (!writeRecord(output, record)) {
            lastErrorText_ = "写入 native 存储失败：记录写入中断。";
            return false;
        }
    }
    lastErrorText_.clear();
    invalidateCachesForFile(fileName);
    return true;
}

bool NativeSessionStore::compactRawEventsIfNeeded() {
    if (rawEventSoftLimitBytes_ == 0 || rawEventTargetBytes_ == 0) {
        return true;
    }

    const auto path = filePath(kRawEventsFile);
    std::error_code error;
    const std::uintmax_t currentSize = std::filesystem::file_size(path, error);
    if (error || currentSize <= rawEventSoftLimitBytes_) {
        return true;
    }

    std::deque<RecordSpan> retainedSpans;
    std::uintmax_t retainedBytes = kHeader.size();
    std::size_t recordCount = 0;
    std::uintmax_t firstRecordOffset = 0;
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            lastErrorText_ = "压缩 native 原始记录失败：无法打开 raw_io_events.svmr。";
            return false;
        }

        if (!skipStoreHeader(input)) {
            lastErrorText_ = "压缩 native 原始记录失败：无法读取文件头。";
            return false;
        }

        firstRecordOffset = static_cast<std::uintmax_t>(input.tellg());
        std::string parseError;
        while (true) {
            std::optional<RecordSpan> span = readNextRecordSpan(input, &parseError);
            if (!parseError.empty()) {
                lastErrorText_ = parseError;
                return false;
            }
            if (!span.has_value()) {
                break;
            }
            ++recordCount;
            while (!retainedSpans.empty() && retainedBytes + span->size > rawEventTargetBytes_) {
                retainedBytes -= retainedSpans.front().size;
                retainedSpans.pop_front();
            }
            retainedBytes += span->size;
            retainedSpans.push_back(*span);
        }
    }

    if (recordCount == 0) {
        return rewriteRecords(kRawEventsFile, {});
    }

    if (retainedSpans.empty() || retainedSpans.front().offset <= firstRecordOffset) {
        return true;
    }

    const auto tempPath = replacementTempPath(path);
    if (!copyFileTailWithHeader(path, tempPath, retainedSpans.front().offset, &lastErrorText_)) {
        std::filesystem::remove(tempPath);
        return false;
    }
    return replaceFileWithTemp(tempPath, path, lastErrorText_, "压缩 native 原始记录");
}

bool NativeSessionStore::visitRecords(std::string_view fileName, const std::function<bool(const Record&)>& visitor) const {
    const auto path = filePath(fileName);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lastErrorText_ = "读取 native 存储失败：无法打开 " + path.filename().string() + "。";
        return false;
    }
    if (!skipStoreHeader(input)) {
        lastErrorText_ = "读取 native 存储失败：无法读取 " + path.filename().string() + " 文件头。";
        return false;
    }

    std::string errorText;
    while (true) {
        std::optional<Record> record = readNextRecordFromStream(input, &errorText, "读取 native 存储");
        if (!errorText.empty()) {
            lastErrorText_ = errorText;
            return false;
        }
        if (!record.has_value()) {
            break;
        }
        if (!visitor(*record)) {
            break;
        }
    }
    lastErrorText_.clear();
    return true;
}

std::vector<NativeSessionStore::Record> NativeSessionStore::loadRecords(std::string_view fileName) const {
    const auto path = filePath(fileName);
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lastErrorText_ = "读取 native 存储失败：无法打开 " + path.filename().string() + "。";
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string errorText;
    std::vector<Record> records = parseRecords(buffer.str(), &errorText);
    if (!errorText.empty()) {
        lastErrorText_ = errorText;
    }
    return records;
}

bool NativeSessionStore::rewriteRecords(std::string_view fileName, const std::vector<Record>& records) {
    absorbRecordIds(fileName, records);
    if (fileName != kIdCountersFile && !persistCounters()) {
        return false;
    }
    const auto path = filePath(fileName);
    const auto tempPath = replacementTempPath(path);
    bool writeFailed = false;
    std::string writeErrorText;
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            lastErrorText_ = "重写 native 存储失败：无法创建临时文件 " + std::filesystem::path(tempPath).filename().string() + "。";
            return false;
        }
        output << kHeader;
        if (!output) {
            writeFailed = true;
            writeErrorText = "重写 native 存储失败：文件头写入中断。";
        } else {
            for (const Record& record : records) {
                if (!writeRecord(output, record)) {
                    writeFailed = true;
                    writeErrorText = "重写 native 存储失败：记录写入中断。";
                    break;
                }
            }
        }
    }
    if (writeFailed) {
        std::filesystem::remove(tempPath);
        lastErrorText_ = writeErrorText;
        return false;
    }

    const bool replaced = replaceFileWithTemp(tempPath, path, lastErrorText_, "重写 native 存储");
    if (replaced) {
        invalidateCachesForFile(fileName);
    }
    return replaced;
}

bool NativeSessionStore::rewriteRecordsFiltered(
    std::string_view fileName,
    const std::function<bool(const Record&)>& keepRecord,
    const std::vector<Record>& appendedRecords) {
    absorbRecordIds(fileName, appendedRecords);
    if (fileName != kIdCountersFile && !persistCounters()) {
        return false;
    }

    const auto path = filePath(fileName);
    const auto tempPath = replacementTempPath(path);
    bool writeFailed = false;
    bool visitFailed = false;
    std::string writeErrorText;
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            lastErrorText_ = "重写 native 存储失败：无法创建临时文件 " + std::filesystem::path(tempPath).filename().string() + "。";
            return false;
        }
        output << kHeader;
        if (!output) {
            writeFailed = true;
            writeErrorText = "重写 native 存储失败：文件头写入中断。";
        } else {
            const bool visited = visitRecords(fileName, [&](const Record& record) {
                if (!keepRecord(record)) {
                    return true;
                }
                if (!writeRecord(output, record)) {
                    writeFailed = true;
                    writeErrorText = "重写 native 存储失败：记录写入中断。";
                    return false;
                }
                return true;
            });
            if (!visited) {
                visitFailed = true;
            }

            if (!visitFailed && !writeFailed) {
                for (const Record& record : appendedRecords) {
                    if (!writeRecord(output, record)) {
                        writeFailed = true;
                        writeErrorText = "重写 native 存储失败：记录写入中断。";
                        break;
                    }
                }
            }
        }
    }
    if (visitFailed) {
        std::filesystem::remove(tempPath);
        return false;
    }
    if (writeFailed) {
        std::filesystem::remove(tempPath);
        lastErrorText_ = writeErrorText;
        return false;
    }

    const bool replaced = replaceFileWithTemp(tempPath, path, lastErrorText_, "重写 native 存储");
    if (replaced) {
        invalidateCachesForFile(fileName);
    }
    return replaced;
}

bool NativeSessionStore::rewriteRecordsFilteredTransaction(std::vector<RewriteRequest> requests) {
    if (requests.empty()) {
        return true;
    }

    for (const RewriteRequest& request : requests) {
        absorbRecordIds(request.fileName, request.appendedRecords);
    }

    if (countersDirty_) {
        requests.push_back(RewriteRequest{
            std::string(kIdCountersFile),
            [](const Record&) { return false; },
            counterRecords(),
        });
    }

    std::vector<ReplacementTarget> targets;
    targets.reserve(requests.size());
    bool writeFailed = false;
    bool visitFailed = false;
    std::string writeErrorText;

    for (const RewriteRequest& request : requests) {
        const auto path = filePath(request.fileName);
        const auto tempPath = replacementTempPath(path);
        bool requestWriteFailed = false;
        bool requestVisitFailed = false;
        {
            std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                writeFailed = true;
                writeErrorText = "事务重写 native 存储失败：无法创建临时文件 "
                    + std::filesystem::path(tempPath).filename().string() + "。";
            } else {
                output << kHeader;
                if (!output) {
                    requestWriteFailed = true;
                    writeErrorText = "事务重写 native 存储失败：文件头写入中断。";
                } else {
                    const bool visited = visitRecords(request.fileName, [&](const Record& record) {
                        if (!request.keepRecord(record)) {
                            return true;
                        }
                        if (!writeRecord(output, record)) {
                            requestWriteFailed = true;
                            writeErrorText = "事务重写 native 存储失败：记录写入中断。";
                            return false;
                        }
                        return true;
                    });
                    if (!visited) {
                        requestVisitFailed = true;
                    }

                    if (!requestVisitFailed && !requestWriteFailed) {
                        for (const Record& record : request.appendedRecords) {
                            if (!writeRecord(output, record)) {
                                requestWriteFailed = true;
                                writeErrorText = "事务重写 native 存储失败：记录写入中断。";
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (writeFailed || requestWriteFailed || requestVisitFailed) {
            writeFailed = writeFailed || requestWriteFailed;
            visitFailed = visitFailed || requestVisitFailed;
            std::filesystem::remove(tempPath);
            break;
        }

        targets.push_back(ReplacementTarget{
            tempPath,
            path,
            replacementBackupPath(path),
            false,
            false,
            false,
        });
    }

    if (visitFailed || writeFailed) {
        for (const ReplacementTarget& target : targets) {
            std::filesystem::remove(target.tempPath);
        }
        if (writeFailed) {
            lastErrorText_ = writeErrorText;
        }
        return false;
    }

    const bool savedCounters = countersDirty_;
    if (!commitReplacementTargets(targets, lastErrorText_, "事务重写 native 存储")) {
        return false;
    }
    if (savedCounters) {
        countersDirty_ = false;
    }
    for (const RewriteRequest& request : requests) {
        invalidateCachesForFile(request.fileName);
    }
    return true;
}

void NativeSessionStore::invalidateCachesForFile(std::string_view fileName) const {
    if (fileName == kScanSessionsFile) {
        scanSessionsCacheValid_ = false;
        scanSessionsCache_.clear();
        scanAttemptsCacheValid_ = false;
        scanObservationsCacheValid_ = false;
        scanAttemptsCache_.clear();
        scanObservationsCache_.clear();
    } else if (fileName == kMatchRunsFile) {
        matchRunsCacheValid_ = false;
        matchRunsCache_.clear();
        matchCandidatesCacheValid_ = false;
        matchCandidatesCache_.clear();
    } else if (fileName == kRuleVerificationRunsFile) {
        ruleVerificationRunsCacheValid_ = false;
        ruleVerificationRunsCache_.clear();
        ruleVerificationResultsCacheValid_ = false;
        ruleVerificationResultsCache_.clear();
    } else if (fileName == kScanAttemptsFile) {
        scanAttemptsCacheValid_ = false;
        scanAttemptsCache_.clear();
    } else if (fileName == kScanObservationsFile) {
        scanObservationsCacheValid_ = false;
        scanObservationsCache_.clear();
    } else if (fileName == kMatchCandidatesFile) {
        matchCandidatesCacheValid_ = false;
        matchCandidatesCache_.clear();
    } else if (fileName == kRuleVerificationResultsFile) {
        ruleVerificationResultsCacheValid_ = false;
        ruleVerificationResultsCache_.clear();
    }
}

const std::vector<ScanSessionRecord>& NativeSessionStore::cachedScanSessions() const {
    if (scanSessionsCacheValid_) {
        return scanSessionsCache_;
    }
    scanSessionsCache_.clear();
    const bool ok = visitRecords(kScanSessionsFile, [&](const Record& record) {
        scanSessionsCache_.push_back(scanSessionFromRecord(record));
        return true;
    });
    if (ok) {
        scanSessionsCacheValid_ = true;
    } else {
        scanSessionsCache_.clear();
    }
    return scanSessionsCache_;
}

const std::vector<MatchRunRecord>& NativeSessionStore::cachedMatchRuns() const {
    if (matchRunsCacheValid_) {
        return matchRunsCache_;
    }
    matchRunsCache_.clear();
    const bool ok = visitRecords(kMatchRunsFile, [&](const Record& record) {
        matchRunsCache_.push_back(matchRunFromRecord(record));
        return true;
    });
    if (ok) {
        matchRunsCacheValid_ = true;
    } else {
        matchRunsCache_.clear();
    }
    return matchRunsCache_;
}

const std::vector<RuleVerificationRunRecord>& NativeSessionStore::cachedRuleVerificationRuns() const {
    if (ruleVerificationRunsCacheValid_) {
        return ruleVerificationRunsCache_;
    }
    ruleVerificationRunsCache_.clear();
    const bool ok = visitRecords(kRuleVerificationRunsFile, [&](const Record& record) {
        ruleVerificationRunsCache_.push_back(verificationRunFromRecord(record));
        return true;
    });
    if (ok) {
        ruleVerificationRunsCacheValid_ = true;
    } else {
        ruleVerificationRunsCache_.clear();
    }
    return ruleVerificationRunsCache_;
}

const std::vector<ScanAttemptRecord>& NativeSessionStore::cachedScanAttempts(std::string_view sessionId) const {
    if (scanAttemptsCacheValid_ && scanAttemptsCacheSessionId_ == sessionId) {
        return scanAttemptsCache_;
    }
    scanAttemptsCacheValid_ = false;
    scanAttemptsCacheSessionId_ = std::string(sessionId);
    scanAttemptsCache_.clear();
    const bool ok = visitRecords(kScanAttemptsFile, [&](const Record& record) {
        ScanAttemptRecord attempt = scanAttemptFromRecord(record);
        if (attempt.sessionId == sessionId) {
            scanAttemptsCache_.push_back(std::move(attempt));
        }
        return true;
    });
    if (!ok) {
        scanAttemptsCache_.clear();
        return scanAttemptsCache_;
    }
    std::sort(scanAttemptsCache_.begin(), scanAttemptsCache_.end(), [](const ScanAttemptRecord& left, const ScanAttemptRecord& right) {
        if (left.blockIndex != right.blockIndex) {
            return left.blockIndex < right.blockIndex;
        }
        if (left.attemptIndex != right.attemptIndex) {
            return left.attemptIndex < right.attemptIndex;
        }
        return left.id < right.id;
    });
    scanAttemptsCacheValid_ = true;
    return scanAttemptsCache_;
}

const std::vector<ScanObservationRecord>& NativeSessionStore::cachedScanObservations(std::string_view sessionId) const {
    if (scanObservationsCacheValid_ && scanObservationsCacheSessionId_ == sessionId) {
        return scanObservationsCache_;
    }
    scanObservationsCacheValid_ = false;
    scanObservationsCacheSessionId_ = std::string(sessionId);
    scanObservationsCache_.clear();
    const bool ok = visitRecords(kScanObservationsFile, [&](const Record& record) {
        ScanObservationRecord observation = scanObservationFromRecord(record);
        if (observation.sessionId == sessionId) {
            scanObservationsCache_.push_back(std::move(observation));
        }
        return true;
    });
    if (!ok) {
        scanObservationsCache_.clear();
        return scanObservationsCache_;
    }
    std::sort(scanObservationsCache_.begin(), scanObservationsCache_.end(), [](const ScanObservationRecord& left, const ScanObservationRecord& right) {
        if (left.address != right.address) {
            return left.address < right.address;
        }
        return left.id < right.id;
    });
    scanObservationsCacheValid_ = true;
    return scanObservationsCache_;
}

const std::vector<MatchCandidateRecord>& NativeSessionStore::cachedMatchCandidates(std::string_view runId) const {
    if (matchCandidatesCacheValid_ && matchCandidatesCacheRunId_ == runId) {
        return matchCandidatesCache_;
    }
    matchCandidatesCacheValid_ = false;
    matchCandidatesCacheRunId_ = std::string(runId);
    matchCandidatesCache_.clear();
    const bool ok = visitRecords(kMatchCandidatesFile, [&](const Record& record) {
        MatchCandidateRecord candidate = matchCandidateFromRecord(record);
        if (candidate.runId == runId) {
            matchCandidatesCache_.push_back(std::move(candidate));
        }
        return true;
    });
    if (!ok) {
        matchCandidatesCache_.clear();
        return matchCandidatesCache_;
    }
    std::sort(matchCandidatesCache_.begin(), matchCandidatesCache_.end(), [](const MatchCandidateRecord& left, const MatchCandidateRecord& right) {
        if (left.rankIndex != right.rankIndex) {
            return left.rankIndex < right.rankIndex;
        }
        return left.id < right.id;
    });
    matchCandidatesCacheValid_ = true;
    return matchCandidatesCache_;
}

const std::vector<RuleVerificationResultRecord>& NativeSessionStore::cachedRuleVerificationResults(std::string_view verificationRunId) const {
    if (ruleVerificationResultsCacheValid_ && ruleVerificationResultsCacheRunId_ == verificationRunId) {
        return ruleVerificationResultsCache_;
    }
    ruleVerificationResultsCacheValid_ = false;
    ruleVerificationResultsCacheRunId_ = std::string(verificationRunId);
    ruleVerificationResultsCache_.clear();
    const bool ok = visitRecords(kRuleVerificationResultsFile, [&](const Record& record) {
        RuleVerificationResultRecord result = verificationResultFromRecord(record);
        if (result.verificationRunId == verificationRunId) {
            ruleVerificationResultsCache_.push_back(std::move(result));
        }
        return true;
    });
    if (!ok) {
        ruleVerificationResultsCache_.clear();
        return ruleVerificationResultsCache_;
    }
    std::sort(ruleVerificationResultsCache_.begin(), ruleVerificationResultsCache_.end(), [](const RuleVerificationResultRecord& left, const RuleVerificationResultRecord& right) {
        return left.id < right.id;
    });
    ruleVerificationResultsCacheValid_ = true;
    return ruleVerificationResultsCache_;
}

std::filesystem::path NativeSessionStore::filePath(std::string_view fileName) const {
    return storeDirectory_ / std::string(fileName);
}

void NativeSessionStore::absorbRecordIds(std::string_view fileName, const std::vector<Record>& records) {
    if (fileName == kIdCountersFile || records.empty()) {
        return;
    }

    const std::string key(fileName);
    std::int64_t& nextId = nextIds_[key];
    if (nextId <= 0) {
        nextId = 1;
    }
    for (const Record& record : records) {
        if (record.empty()) {
            continue;
        }
        const std::int64_t usedId = toInt64(record[0], 0);
        if (usedId >= nextId) {
            nextId = usedId + 1;
            countersDirty_ = true;
        }
    }
}

std::int64_t NativeSessionStore::allocateId(std::string_view fileName) {
    std::string key(fileName);
    std::int64_t& nextId = nextIds_[key];
    if (nextId <= 0) {
        nextId = 1;
    }
    const std::int64_t allocated = nextId;
    ++nextId;
    countersDirty_ = true;
    return allocated;
}

std::vector<NativeSessionStore::Record> NativeSessionStore::counterRecords() const {
    std::vector<Record> records;
    records.reserve(storeFiles().size());
    for (std::string_view fileName : storeFiles()) {
        const std::string key(fileName);
        const auto iterator = nextIds_.find(key);
        const std::int64_t nextId = iterator == nextIds_.end() ? 1 : std::max<std::int64_t>(1, iterator->second);
        records.push_back({key, toString(nextId)});
    }
    return records;
}

bool NativeSessionStore::loadPersistedCounters() {
    const auto path = filePath(kIdCountersFile);
    if (!std::filesystem::exists(path)) {
        return false;
    }

    const std::vector<Record> records = loadRecords(kIdCountersFile);
    std::unordered_map<std::string, std::int64_t> loaded;
    loaded.reserve(records.size());
    for (const Record& record : records) {
        if (record.size() < 2) {
            lastErrorText_.clear();
            return false;
        }
        const std::int64_t nextId = toInt64(record[1], -1);
        if (record[0].empty() || nextId <= 0) {
            lastErrorText_.clear();
            return false;
        }
        loaded[record[0]] = nextId;
    }

    for (std::string_view fileName : storeFiles()) {
        if (loaded.find(std::string(fileName)) == loaded.end()) {
            lastErrorText_.clear();
            return false;
        }
    }

    nextIds_ = std::move(loaded);
    countersDirty_ = false;
    lastErrorText_.clear();
    return true;
}

bool NativeSessionStore::persistCounters() {
    if (!countersDirty_) {
        return true;
    }
    const bool saved = rewriteRecords(kIdCountersFile, counterRecords());
    if (saved) {
        countersDirty_ = false;
    }
    return saved;
}

bool NativeSessionStore::reloadCounters() {
    nextIds_.clear();
    for (std::string_view fileName : storeFiles()) {
        std::int64_t maxId = 0;
        const bool ok = visitRecords(fileName, [&](const Record& record) {
            if (!record.empty()) {
                maxId = std::max(maxId, toInt64(record[0]));
            }
            return true;
        });
        if (!ok) {
            return false;
        }
        nextIds_[std::string(fileName)] = maxId + 1;
    }
    countersDirty_ = true;
    return persistCounters();
}

} // namespace svm::native_storage
