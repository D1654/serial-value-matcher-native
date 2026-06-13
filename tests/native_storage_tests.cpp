#include "native_storage/native_session_store.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path temporaryStorePath() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("svm-native-storage-test-" + std::to_string(suffix));
}

svm::native_storage::NativeSessionStore openStore(const std::filesystem::path& path) {
    svm::native_storage::NativeSessionStore store;
    assert(store.open(path));
    assert(store.isOpen());
    assert(std::filesystem::exists(path / "schema.txt"));
    return store;
}

void rawEventsAreBatchedAndReopened() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::RawIoEvent tx;
    tx.sessionId = "session-1";
    tx.direction = "Tx";
    tx.timestampUtc = "2026-06-12T10:00:00Z";
    tx.endpoint = "COM3";
    tx.payload = {0x01, 0x03, 0x00, 0x00};

    svm::native_storage::RawIoEvent rx = tx;
    rx.direction = "Rx";
    rx.payload = {0x01, 0x03, 0x02, 0x12, 0x34};

    assert(store.appendRawEvents({tx, rx}));
    assert(store.rawEventCount() == 2);
    const auto recent = store.recentRawEvents(1);
    assert(recent.size() == 1);
    assert(recent[0].direction == "Rx");
    assert((recent[0].payload == std::vector<std::uint8_t>{0x01, 0x03, 0x02, 0x12, 0x34}));

    auto reopened = openStore(path);
    assert(reopened.rawEventCount() == 2);

    std::filesystem::remove_all(path);
}

void sendHistoryAndProfilesKeepLatestRecords() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::SendHistoryEntry first;
    first.content = "AT+测试";
    first.payloadMode = 0;
    first.lineEnding = 1;
    first.textEncodingCodePage = 65001;
    first.sentAtUtc = "2026-06-12T10:00:00Z";
    assert(store.saveSendHistory(first, 2));

    svm::native_storage::SendHistoryEntry duplicate = first;
    duplicate.sentAtUtc = "2026-06-12T10:01:00Z";
    assert(store.saveSendHistory(duplicate, 2));

    svm::native_storage::SendHistoryEntry second;
    second.content = "010300000001";
    second.payloadMode = 1;
    second.lineEnding = 0;
    second.textEncodingCodePage = 936;
    second.sentAtUtc = "2026-06-12T10:02:00Z";
    assert(store.saveSendHistory(second, 2));

    const auto history = store.recentSendHistory(10);
    assert(history.size() == 2);
    assert(history[0].content == "010300000001");
    assert(history[0].textEncodingCodePage == 936);
    assert(history[1].content == "AT+测试");
    assert(history[1].sentAtUtc == "2026-06-12T10:01:00Z");
    assert(history[1].textEncodingCodePage == 65001);

    svm::native_storage::SerialProfile profile;
    profile.name = "default";
    profile.portName = "COM10";
    profile.baudRate = 921600;
    profile.parity = "Even";
    profile.dataTerminalReady = true;
    profile.updatedAtUtc = "2026-06-12T10:03:00Z";
    assert(store.saveSerialProfile(profile));

    auto latest = store.latestSerialProfile();
    assert(latest.has_value());
    assert(latest->portName == "COM10");
    assert(latest->baudRate == 921600);
    assert(latest->dataTerminalReady);

    std::filesystem::remove_all(path);
}

void uiPreferencesRoundTrip() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::UiPreferences preferences;
    preferences.name = "default";
    preferences.logThemeIndex = 2;
    preferences.logFormat = 4;
    preferences.logEncodingCodePage = 936;
    preferences.showLogTimestamps = false;
    preferences.sendPayloadMode = 1;
    preferences.sendTextEncodingCodePage = 936;
    preferences.sendLineEnding = 3;
    preferences.windowLeft = 20;
    preferences.windowTop = 30;
    preferences.windowWidth = 1280;
    preferences.windowHeight = 820;
    preferences.updatedAtUtc = "2026-06-13T00:00:00Z";
    assert(store.saveUiPreferences(preferences));

    auto reopened = openStore(path);
    const auto loaded = reopened.latestUiPreferences();
    assert(loaded.has_value());
    assert(loaded->logThemeIndex == 2);
    assert(loaded->logFormat == 4);
    assert(loaded->logEncodingCodePage == 936);
    assert(!loaded->showLogTimestamps);
    assert(loaded->sendPayloadMode == 1);
    assert(loaded->sendTextEncodingCodePage == 936);
    assert(loaded->sendLineEnding == 3);
    assert(loaded->windowLeft == 20);
    assert(loaded->windowTop == 30);
    assert(loaded->windowWidth == 1280);
    assert(loaded->windowHeight == 820);

    std::filesystem::remove_all(path);
}

void scanAndMatchRecordsRoundTrip() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::ScanExecutionRecord execution;
    execution.session.sessionId = "scan-1";
    execution.session.slaveId = 1;
    execution.session.functionCode = 3;
    execution.session.startAddress = 100;
    execution.session.endAddress = 101;
    execution.session.blockSize = 2;
    execution.session.requestCount = 1;
    execution.session.status = "completed";
    execution.session.startedAtUtc = "2026-06-12T10:04:00Z";
    execution.session.finishedAtUtc = "2026-06-12T10:04:01Z";
    execution.session.successBlockCount = 1;

    svm::native_storage::ScanAttemptRecord attempt;
    attempt.blockIndex = 0;
    attempt.attemptIndex = 0;
    attempt.startAddress = 100;
    attempt.quantity = 2;
    attempt.status = "success";
    attempt.requestFrame = {0x01, 0x03};
    attempt.responseFrame = {0x01, 0x03, 0x04, 0x12, 0x34};
    attempt.endpoint = "COM3";
    execution.attempts.push_back(attempt);

    svm::native_storage::ScanObservationRecord observation;
    observation.blockIndex = 0;
    observation.attemptIndex = 0;
    observation.slaveId = 1;
    observation.functionCode = 3;
    observation.address = 100;
    observation.value = 0x1234;
    observation.observedAtUtc = "2026-06-12T10:04:01Z";
    execution.observations.push_back(observation);

    assert(store.saveScanExecution(execution));
    const auto latestSession = store.latestScanSession();
    assert(latestSession.has_value());
    assert(latestSession->sessionId == "scan-1");
    const auto recentSessions = store.recentScanSessions(5);
    assert(recentSessions.size() == 1);
    assert(recentSessions[0].sessionId == "scan-1");
    const auto loadedSession = store.scanSession("scan-1");
    assert(loadedSession.has_value());
    assert(loadedSession->status == "completed");
    assert(store.scanAttempts("scan-1").size() == 1);
    const auto observations = store.scanObservations("scan-1");
    assert(observations.size() == 1);
    assert(observations[0].value == 0x1234);

    svm::native_storage::MatchRunRecord run;
    run.runId = "match-1";
    run.sourceScanSessionId = "scan-1";
    run.targetLabel = "温度";
    run.targetValue = 12.34;
    run.targetUnit = "℃";
    run.createdAtUtc = "2026-06-12T10:05:00Z";

    svm::native_storage::MatchCandidateRecord candidate;
    candidate.candidateType = "UInt16";
    candidate.wordOrder = "HighWordFirst";
    candidate.byteOrder = "BigEndian";
    candidate.sourceSessionId = "scan-1";
    candidate.slaveId = 1;
    candidate.functionCode = 3;
    candidate.startAddress = 100;
    candidate.registerCount = 1;
    candidate.observationIds = {1};
    candidate.addresses = {100};
    candidate.rawRegisters = {0x1234};
    candidate.engineeringValue = 12.34;
    candidate.score = 100.0;
    candidate.evidenceText = "单样本候选";

    assert(store.saveMatchRun(run, {candidate}));
    const auto latestRun = store.latestMatchRun();
    assert(latestRun.has_value());
    assert(latestRun->runId == "match-1");
    const auto recentRuns = store.recentMatchRuns(5);
    assert(recentRuns.size() == 1);
    assert(recentRuns[0].sourceScanSessionId == "scan-1");
    const auto loadedRun = store.matchRun("match-1");
    assert(loadedRun.has_value());
    assert(loadedRun->candidateCount == 1);
    const auto candidates = store.matchCandidates("match-1");
    assert(candidates.size() == 1);
    assert(candidates[0].candidateType == "UInt16");
    assert(candidates[0].evidenceText == "单样本候选");

    std::filesystem::remove_all(path);
}

void protocolRulesAndVerificationRoundTrip() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = "rule-1";
    rule.fieldName = "运行状态";
    rule.candidateType = "BitFlags";
    rule.wordOrder = "HighWordFirst";
    rule.byteOrder = "BigEndian";
    rule.slaveId = 1;
    rule.functionCode = 3;
    rule.startAddress = 200;
    rule.registerCount = 1;
    rule.confidenceLevel = "高";
    rule.evidenceSummary = "置信等级 高";
    rule.interpretationMap = "0=运行允许|未允许|已允许";
    rule.createdAtUtc = "2026-06-12T10:06:00Z";
    assert(store.saveProtocolFieldRule(rule));

    auto loadedRule = store.protocolFieldRule("rule-1");
    assert(loadedRule.has_value());
    assert(loadedRule->fieldName == "运行状态");
    assert(loadedRule->interpretationMap.find("运行允许") != std::string::npos);

    svm::native_storage::RuleVerificationRunRecord verificationRun;
    verificationRun.verificationRunId = "verify-1";
    verificationRun.sourceScanSessionId = "scan-1";
    verificationRun.ruleCount = 1;
    verificationRun.verifiedCount = 1;
    verificationRun.createdAtUtc = "2026-06-12T10:07:00Z";

    svm::native_storage::RuleVerificationResultRecord verificationResult;
    verificationResult.ruleId = "rule-1";
    verificationResult.fieldName = "运行状态";
    verificationResult.candidateType = "BitFlags";
    verificationResult.sourceScanSessionId = "scan-1";
    verificationResult.verified = true;
    verificationResult.statusText = "已验证";
    verificationResult.slaveId = 1;
    verificationResult.functionCode = 3;
    verificationResult.startAddress = 200;
    verificationResult.registerCount = 1;
    verificationResult.observationIds = {1};
    verificationResult.rawRegisters = {1};
    verificationResult.interpretationText = "位解释：bit0 运行允许=已允许。";

    assert(store.saveRuleVerificationRun(verificationRun, {verificationResult}));
    const auto latestVerificationRun = store.latestRuleVerificationRun();
    assert(latestVerificationRun.has_value());
    assert(latestVerificationRun->verificationRunId == "verify-1");
    const auto loadedVerificationRun = store.ruleVerificationRun("verify-1");
    assert(loadedVerificationRun.has_value());
    assert(loadedVerificationRun->verifiedCount == 1);
    const auto results = store.ruleVerificationResults("verify-1");
    assert(results.size() == 1);
    assert(results[0].verified);
    assert(results[0].interpretationText.find("已允许") != std::string::npos);

    assert(store.deleteProtocolFieldRule("rule-1"));
    assert(!store.protocolFieldRule("rule-1").has_value());

    std::filesystem::remove_all(path);
}

} // namespace

int main() {
    rawEventsAreBatchedAndReopened();
    sendHistoryAndProfilesKeepLatestRecords();
    uiPreferencesRoundTrip();
    scanAndMatchRecordsRoundTrip();
    protocolRulesAndVerificationRoundTrip();

    std::cout << "native_storage_tests passed\n";
    return 0;
}
