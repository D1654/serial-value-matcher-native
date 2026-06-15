#include "native_storage/native_session_store.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace svm::native_storage {
namespace {

constexpr std::string_view kHeader = "SVM_NATIVE_STORE_V1\n";
constexpr std::string_view kSchemaFile = "schema.txt";
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

bool writeRecord(std::ostream& output, const NativeSessionStore::Record& record) {
    output << record.size() << '|';
    for (const std::string& field : record) {
        output << field.size() << ':';
        output.write(field.data(), static_cast<std::streamsize>(field.size()));
        if (!output) {
            return false;
        }
    }
    output << '\n';
    return static_cast<bool>(output);
}

bool parseUnsigned(const std::string& data, std::size_t& position, char delimiter, std::size_t& value) {
    const std::size_t delimiterPosition = data.find(delimiter, position);
    if (delimiterPosition == std::string::npos || delimiterPosition == position) {
        return false;
    }
    std::size_t parsed = 0;
    const char* begin = data.data() + position;
    const char* end = data.data() + delimiterPosition;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    value = parsed;
    position = delimiterPosition + 1;
    return true;
}

std::vector<NativeSessionStore::Record> parseRecords(const std::string& data, std::string* errorText) {
    std::vector<NativeSessionStore::Record> records;
    std::size_t position = data.rfind(std::string(kHeader), 0) == 0 ? kHeader.size() : 0;
    while (position < data.size()) {
        while (position < data.size() && (data[position] == '\n' || data[position] == '\r')) {
            ++position;
        }
        if (position >= data.size()) {
            break;
        }

        std::size_t fieldCount = 0;
        if (!parseUnsigned(data, position, '|', fieldCount)) {
            if (errorText != nullptr) {
                *errorText = "读取 native 存储失败：记录字段数量损坏。";
            }
            return {};
        }

        NativeSessionStore::Record record;
        record.reserve(fieldCount);
        for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
            std::size_t fieldLength = 0;
            if (!parseUnsigned(data, position, ':', fieldLength) || position + fieldLength > data.size()) {
                if (errorText != nullptr) {
                    *errorText = "读取 native 存储失败：记录字段长度损坏。";
                }
                return {};
            }
            record.push_back(data.substr(position, fieldLength));
            position += fieldLength;
        }

        if (position < data.size() && data[position] == '\n') {
            ++position;
        }
        records.push_back(std::move(record));
    }
    return records;
}

template <typename T>
std::vector<T> recentLastFirst(std::vector<T> values, int limit) {
    const std::size_t safeLimit = static_cast<std::size_t>(std::max(1, limit));
    std::vector<T> result;
    result.reserve(std::min(safeLimit, values.size()));
    for (auto iterator = values.rbegin(); iterator != values.rend() && result.size() < safeLimit; ++iterator) {
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
    }

    for (std::string_view fileName : storeFiles()) {
        const auto path = filePath(fileName);
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

    reloadCounters();
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
    return appendRecords(kRawEventsFile, records);
}

std::int64_t NativeSessionStore::rawEventCount() const {
    return static_cast<std::int64_t>(loadRecords(kRawEventsFile).size());
}

std::vector<RawIoEvent> NativeSessionStore::recentRawEvents(std::size_t limit) const {
    std::vector<RawIoEvent> events;
    for (const Record& record : loadRecords(kRawEventsFile)) {
        events.push_back(rawEventFromRecord(record));
    }
    return recentLastFirst(std::move(events), static_cast<int>(limit));
}

bool NativeSessionStore::saveSendHistory(SendHistoryEntry entry, int limit) {
    if (!ensureOpen("保存发送历史")) {
        return false;
    }
    if (entry.id <= 0) {
        entry.id = allocateId(kSendHistoryFile);
    }

    std::vector<SendHistoryEntry> entries;
    for (const Record& record : loadRecords(kSendHistoryFile)) {
        SendHistoryEntry existing = sendHistoryFromRecord(record);
        if (existing.content == entry.content
            && existing.payloadMode == entry.payloadMode
            && existing.lineEnding == entry.lineEnding
            && existing.textEncodingCodePage == entry.textEncodingCodePage) {
            continue;
        }
        entries.push_back(std::move(existing));
    }
    entries.push_back(std::move(entry));

    const std::size_t safeLimit = static_cast<std::size_t>(std::max(1, limit));
    if (entries.size() > safeLimit) {
        entries.erase(entries.begin(), entries.end() - static_cast<std::ptrdiff_t>(safeLimit));
    }

    std::vector<Record> records;
    records.reserve(entries.size());
    for (const SendHistoryEntry& item : entries) {
        records.push_back(recordFromSendHistory(item));
    }
    return rewriteRecords(kSendHistoryFile, records);
}

std::vector<SendHistoryEntry> NativeSessionStore::recentSendHistory(int limit) const {
    std::vector<SendHistoryEntry> entries;
    for (const Record& record : loadRecords(kSendHistoryFile)) {
        entries.push_back(sendHistoryFromRecord(record));
    }
    return recentLastFirst(std::move(entries), limit);
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

    std::vector<SerialProfile> profiles;
    for (const Record& record : loadRecords(kSerialProfilesFile)) {
        SerialProfile existing = serialProfileFromRecord(record);
        if (existing.name != profile.name) {
            profiles.push_back(std::move(existing));
        }
    }
    profiles.push_back(std::move(profile));

    std::vector<Record> records;
    records.reserve(profiles.size());
    for (const SerialProfile& item : profiles) {
        records.push_back(recordFromSerialProfile(item));
    }
    return rewriteRecords(kSerialProfilesFile, records);
}

std::optional<SerialProfile> NativeSessionStore::latestSerialProfile() const {
    std::optional<SerialProfile> latest;
    for (const Record& record : loadRecords(kSerialProfilesFile)) {
        latest = serialProfileFromRecord(record);
    }
    return latest;
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

    std::vector<UiPreferences> allPreferences;
    for (const Record& record : loadRecords(kUiPreferencesFile)) {
        UiPreferences existing = uiPreferencesFromRecord(record);
        if (existing.name != preferences.name) {
            allPreferences.push_back(std::move(existing));
        }
    }
    allPreferences.push_back(std::move(preferences));

    std::vector<Record> records;
    records.reserve(allPreferences.size());
    for (const UiPreferences& item : allPreferences) {
        records.push_back(recordFromUiPreferences(item));
    }
    return rewriteRecords(kUiPreferencesFile, records);
}

std::optional<UiPreferences> NativeSessionStore::latestUiPreferences() const {
    std::optional<UiPreferences> latest;
    for (const Record& record : loadRecords(kUiPreferencesFile)) {
        latest = uiPreferencesFromRecord(record);
    }
    return latest;
}

bool NativeSessionStore::saveScanExecution(const ScanExecutionRecord& execution) {
    if (!ensureOpen("保存扫描结果")) {
        return false;
    }
    if (execution.session.sessionId.empty()) {
        lastErrorText_ = "保存扫描结果失败：扫描会话 ID 不能为空。";
        return false;
    }

    std::vector<Record> sessions;
    for (const Record& record : loadRecords(kScanSessionsFile)) {
        if (scanSessionFromRecord(record).sessionId != execution.session.sessionId) {
            sessions.push_back(record);
        }
    }
    sessions.push_back(recordFromScanSession(execution.session));

    std::vector<Record> attempts;
    for (const Record& record : loadRecords(kScanAttemptsFile)) {
        if (scanAttemptFromRecord(record).sessionId != execution.session.sessionId) {
            attempts.push_back(record);
        }
    }
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
    for (const Record& record : loadRecords(kScanObservationsFile)) {
        if (scanObservationFromRecord(record).sessionId != execution.session.sessionId) {
            observations.push_back(record);
        }
    }
    for (ScanObservationRecord observation : execution.observations) {
        if (observation.sessionId.empty()) {
            observation.sessionId = execution.session.sessionId;
        }
        if (observation.id <= 0) {
            observation.id = allocateId(kScanObservationsFile);
        }
        observations.push_back(recordFromScanObservation(observation));
    }

    return rewriteRecords(kScanSessionsFile, sessions)
        && rewriteRecords(kScanAttemptsFile, attempts)
        && rewriteRecords(kScanObservationsFile, observations);
}

std::vector<ScanSessionRecord> NativeSessionStore::recentScanSessions(int limit) const {
    std::vector<ScanSessionRecord> sessions;
    for (const Record& record : loadRecords(kScanSessionsFile)) {
        sessions.push_back(scanSessionFromRecord(record));
    }
    return recentLastFirst(std::move(sessions), limit);
}

std::optional<ScanSessionRecord> NativeSessionStore::latestScanSession() const {
    const auto sessions = recentScanSessions(1);
    if (sessions.empty()) {
        return std::nullopt;
    }
    return sessions.front();
}

std::optional<ScanSessionRecord> NativeSessionStore::scanSession(std::string_view sessionId) const {
    std::optional<ScanSessionRecord> found;
    for (const Record& record : loadRecords(kScanSessionsFile)) {
        ScanSessionRecord session = scanSessionFromRecord(record);
        if (session.sessionId == sessionId) {
            found = std::move(session);
        }
    }
    return found;
}

std::vector<ScanAttemptRecord> NativeSessionStore::scanAttempts(std::string_view sessionId) const {
    std::vector<ScanAttemptRecord> attempts;
    for (const Record& record : loadRecords(kScanAttemptsFile)) {
        ScanAttemptRecord attempt = scanAttemptFromRecord(record);
        if (attempt.sessionId == sessionId) {
            attempts.push_back(std::move(attempt));
        }
    }
    std::sort(attempts.begin(), attempts.end(), [](const ScanAttemptRecord& left, const ScanAttemptRecord& right) {
        if (left.blockIndex != right.blockIndex) {
            return left.blockIndex < right.blockIndex;
        }
        if (left.attemptIndex != right.attemptIndex) {
            return left.attemptIndex < right.attemptIndex;
        }
        return left.id < right.id;
    });
    return attempts;
}

std::vector<ScanObservationRecord> NativeSessionStore::scanObservations(std::string_view sessionId) const {
    std::vector<ScanObservationRecord> observations;
    for (const Record& record : loadRecords(kScanObservationsFile)) {
        ScanObservationRecord observation = scanObservationFromRecord(record);
        if (observation.sessionId == sessionId) {
            observations.push_back(std::move(observation));
        }
    }
    std::sort(observations.begin(), observations.end(), [](const ScanObservationRecord& left, const ScanObservationRecord& right) {
        if (left.address != right.address) {
            return left.address < right.address;
        }
        return left.id < right.id;
    });
    return observations;
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

    std::vector<Record> runs;
    for (const Record& record : loadRecords(kMatchRunsFile)) {
        if (matchRunFromRecord(record).runId != run.runId) {
            runs.push_back(record);
        }
    }
    runs.push_back(recordFromMatchRun(run));

    std::vector<Record> candidateRecords;
    for (const Record& record : loadRecords(kMatchCandidatesFile)) {
        if (matchCandidateFromRecord(record).runId != run.runId) {
            candidateRecords.push_back(record);
        }
    }
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

    return rewriteRecords(kMatchRunsFile, runs)
        && rewriteRecords(kMatchCandidatesFile, candidateRecords);
}

std::vector<MatchRunRecord> NativeSessionStore::recentMatchRuns(int limit) const {
    std::vector<MatchRunRecord> runs;
    for (const Record& record : loadRecords(kMatchRunsFile)) {
        runs.push_back(matchRunFromRecord(record));
    }
    return recentLastFirst(std::move(runs), limit);
}

std::optional<MatchRunRecord> NativeSessionStore::latestMatchRun() const {
    const auto runs = recentMatchRuns(1);
    if (runs.empty()) {
        return std::nullopt;
    }
    return runs.front();
}

std::optional<MatchRunRecord> NativeSessionStore::matchRun(std::string_view runId) const {
    std::optional<MatchRunRecord> found;
    for (const Record& record : loadRecords(kMatchRunsFile)) {
        MatchRunRecord run = matchRunFromRecord(record);
        if (run.runId == runId) {
            found = std::move(run);
        }
    }
    return found;
}

std::vector<MatchCandidateRecord> NativeSessionStore::matchCandidates(std::string_view runId) const {
    std::vector<MatchCandidateRecord> candidates;
    for (const Record& record : loadRecords(kMatchCandidatesFile)) {
        MatchCandidateRecord candidate = matchCandidateFromRecord(record);
        if (candidate.runId == runId) {
            candidates.push_back(std::move(candidate));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const MatchCandidateRecord& left, const MatchCandidateRecord& right) {
        if (left.rankIndex != right.rankIndex) {
            return left.rankIndex < right.rankIndex;
        }
        return left.id < right.id;
    });
    return candidates;
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

    std::vector<Record> records;
    for (const Record& record : loadRecords(kProtocolRulesFile)) {
        if (protocolRuleFromRecord(record).ruleId != rule.ruleId) {
            records.push_back(record);
        }
    }
    records.push_back(recordFromProtocolRule(rule));
    return rewriteRecords(kProtocolRulesFile, records);
}

bool NativeSessionStore::deleteProtocolFieldRule(std::string_view ruleId) {
    if (!ensureOpen("删除协议字段规则")) {
        return false;
    }
    std::vector<Record> records;
    for (const Record& record : loadRecords(kProtocolRulesFile)) {
        if (protocolRuleFromRecord(record).ruleId != ruleId) {
            records.push_back(record);
        }
    }
    return rewriteRecords(kProtocolRulesFile, records);
}

std::optional<ProtocolFieldRuleRecord> NativeSessionStore::protocolFieldRule(std::string_view ruleId) const {
    std::optional<ProtocolFieldRuleRecord> found;
    for (const Record& record : loadRecords(kProtocolRulesFile)) {
        ProtocolFieldRuleRecord rule = protocolRuleFromRecord(record);
        if (rule.ruleId == ruleId) {
            found = std::move(rule);
        }
    }
    return found;
}

std::vector<ProtocolFieldRuleRecord> NativeSessionStore::recentProtocolFieldRules(int limit) const {
    std::vector<ProtocolFieldRuleRecord> rules;
    for (const Record& record : loadRecords(kProtocolRulesFile)) {
        rules.push_back(protocolRuleFromRecord(record));
    }
    return recentLastFirst(std::move(rules), limit);
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

    std::vector<Record> runs;
    for (const Record& record : loadRecords(kRuleVerificationRunsFile)) {
        if (verificationRunFromRecord(record).verificationRunId != run.verificationRunId) {
            runs.push_back(record);
        }
    }
    runs.push_back(recordFromVerificationRun(run));

    std::vector<Record> resultRecords;
    for (const Record& record : loadRecords(kRuleVerificationResultsFile)) {
        if (verificationResultFromRecord(record).verificationRunId != run.verificationRunId) {
            resultRecords.push_back(record);
        }
    }
    for (RuleVerificationResultRecord result : results) {
        if (result.verificationRunId.empty()) {
            result.verificationRunId = run.verificationRunId;
        }
        if (result.id <= 0) {
            result.id = allocateId(kRuleVerificationResultsFile);
        }
        resultRecords.push_back(recordFromVerificationResult(result));
    }

    return rewriteRecords(kRuleVerificationRunsFile, runs)
        && rewriteRecords(kRuleVerificationResultsFile, resultRecords);
}

std::optional<RuleVerificationRunRecord> NativeSessionStore::latestRuleVerificationRun() const {
    std::optional<RuleVerificationRunRecord> latest;
    for (const Record& record : loadRecords(kRuleVerificationRunsFile)) {
        latest = verificationRunFromRecord(record);
    }
    return latest;
}

std::optional<RuleVerificationRunRecord> NativeSessionStore::ruleVerificationRun(std::string_view verificationRunId) const {
    std::optional<RuleVerificationRunRecord> found;
    for (const Record& record : loadRecords(kRuleVerificationRunsFile)) {
        RuleVerificationRunRecord run = verificationRunFromRecord(record);
        if (run.verificationRunId == verificationRunId) {
            found = std::move(run);
        }
    }
    return found;
}

std::vector<RuleVerificationResultRecord> NativeSessionStore::ruleVerificationResults(std::string_view verificationRunId) const {
    std::vector<RuleVerificationResultRecord> results;
    for (const Record& record : loadRecords(kRuleVerificationResultsFile)) {
        RuleVerificationResultRecord result = verificationResultFromRecord(record);
        if (result.verificationRunId == verificationRunId) {
            results.push_back(std::move(result));
        }
    }
    std::sort(results.begin(), results.end(), [](const RuleVerificationResultRecord& left, const RuleVerificationResultRecord& right) {
        return left.id < right.id;
    });
    return results;
}

bool NativeSessionStore::ensureOpen(std::string_view operation) const {
    if (opened_) {
        return true;
    }
    lastErrorText_ = std::string(operation) + "失败：native 存储尚未打开。";
    return false;
}

bool NativeSessionStore::appendRecord(std::string_view fileName, const Record& record) {
    return appendRecords(fileName, {record});
}

bool NativeSessionStore::appendRecords(std::string_view fileName, const std::vector<Record>& records) {
    if (records.empty()) {
        return true;
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
    const auto path = filePath(fileName);
    const auto tempPath = path.string() + ".tmp";
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            lastErrorText_ = "重写 native 存储失败：无法创建临时文件 " + std::filesystem::path(tempPath).filename().string() + "。";
            return false;
        }
        output << kHeader;
        for (const Record& record : records) {
            if (!writeRecord(output, record)) {
                lastErrorText_ = "重写 native 存储失败：记录写入中断。";
                return false;
            }
        }
    }

    std::error_code copyError;
    std::filesystem::copy_file(tempPath, path, std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        if (removeError) {
            std::filesystem::remove(tempPath);
            lastErrorText_ = "重写 native 存储失败：" + copyError.message() + "；删除旧文件失败：" + removeError.message();
            return false;
        }

        std::error_code renameError;
        std::filesystem::rename(tempPath, path, renameError);
        if (renameError) {
            std::filesystem::remove(tempPath);
            lastErrorText_ = "重写 native 存储失败：" + copyError.message() + "；替换临时文件失败：" + renameError.message();
            return false;
        }
    } else {
        std::filesystem::remove(tempPath);
    }
    lastErrorText_.clear();
    return true;
}

std::filesystem::path NativeSessionStore::filePath(std::string_view fileName) const {
    return storeDirectory_ / std::string(fileName);
}

std::int64_t NativeSessionStore::allocateId(std::string_view fileName) {
    std::string key(fileName);
    std::int64_t& nextId = nextIds_[key];
    if (nextId <= 0) {
        nextId = 1;
    }
    const std::int64_t allocated = nextId;
    ++nextId;
    return allocated;
}

void NativeSessionStore::reloadCounters() {
    nextIds_.clear();
    for (std::string_view fileName : storeFiles()) {
        std::int64_t maxId = 0;
        for (const Record& record : loadRecords(fileName)) {
            if (!record.empty()) {
                maxId = std::max(maxId, toInt64(record[0]));
            }
        }
        nextIds_[std::string(fileName)] = maxId + 1;
    }
}

} // namespace svm::native_storage
