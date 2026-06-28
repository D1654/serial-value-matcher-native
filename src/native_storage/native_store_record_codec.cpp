#include "native_storage/native_store_record_codec.h"

#include <charconv>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace svm::native_storage::store_records {
namespace {

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

} // namespace

std::string toString(std::int64_t value) {
    return std::to_string(value);
}

std::int64_t toInt64(const std::string& value, std::int64_t fallback) {
    std::int64_t parsed = fallback;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} ? parsed : fallback;
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
    if (record.size() >= 31) {
        preferences.workbenchHeight = toInt(record[30]);
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
    record.push_back(toString(preferences.workbenchHeight));
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

} // namespace svm::native_storage::store_records
