#include "native_storage/native_session_store.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename Function>
void runStorageTest(const char* name, Function function) {
    std::cerr << "native_storage_tests: " << name << std::endl;
    function();
}

void storageCheckpoint(const char* name) {
    std::cerr << "native_storage_tests:   " << name << std::endl;
}

bool pathExists(const std::filesystem::path& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        std::cerr << "native_storage_tests: exists failed for " << path.string()
                  << ": " << error.message() << std::endl;
        assert(false);
    }
    return exists;
}

std::filesystem::path temporaryStorePath() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("svm-native-storage-test-" + std::to_string(suffix));
}

svm::native_storage::NativeSessionStore openStore(const std::filesystem::path& path) {
    svm::native_storage::NativeSessionStore store;
    assert(store.open(path));
    assert(store.isOpen());
    assert(std::filesystem::exists(path / "schema.txt"));
    assert(std::filesystem::exists(path / "id_counters.svmr"));
    return store;
}

void setReplaceFailureInjection(const char* fileName) {
#if defined(_WIN32)
    _putenv_s("SVM_NATIVE_STORE_FAIL_REPLACE_FILE", fileName == nullptr ? "" : fileName);
#else
    if (fileName == nullptr) {
        unsetenv("SVM_NATIVE_STORE_FAIL_REPLACE_FILE");
    } else {
        setenv("SVM_NATIVE_STORE_FAIL_REPLACE_FILE", fileName, 1);
    }
#endif
}

void writeEmptyStoreFile(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << "SVM_NATIVE_STORE_V1\n";
    assert(output);
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

void replacementArtifactsAreRecoveredOnOpen() {
    const auto path = temporaryStorePath();
    storageCheckpoint("create store with send history");
    {
        auto store = openStore(path);
        svm::native_storage::SendHistoryEntry entry;
        entry.content = "AA55";
        entry.payloadMode = 1;
        entry.sentAtUtc = "2026-06-12T10:00:00Z";
        assert(store.saveSendHistory(entry, 10));
    }

    const auto historyPath = path / "send_history.svmr";
    const auto backupPath = path / "send_history.svmr.bak";
    const auto tempPath = path / "send_history.svmr.tmp";
    storageCheckpoint("move live history to backup");
    std::error_code error;
    std::filesystem::rename(historyPath, backupPath, error);
    if (error) {
        std::cerr << "native_storage_tests: rename failed: " << error.message() << std::endl;
    }
    assert(!error);
    storageCheckpoint("write stale temp artifact");
    {
        std::ofstream temp(tempPath, std::ios::binary | std::ios::trunc);
        temp << "stale temp";
    }
    std::cerr << "native_storage_tests:   before recovery live=" << pathExists(historyPath)
              << " backup=" << pathExists(backupPath)
              << " temp=" << pathExists(tempPath) << std::endl;
    assert(!pathExists(historyPath));
    assert(pathExists(backupPath));
    assert(pathExists(tempPath));

    storageCheckpoint("open store to recover replacement artifacts");
    auto reopened = openStore(path);
    std::cerr << "native_storage_tests:   after recovery live=" << pathExists(historyPath)
              << " backup=" << pathExists(backupPath)
              << " temp=" << pathExists(tempPath) << std::endl;
    assert(pathExists(historyPath));
    assert(!pathExists(backupPath));
    assert(!pathExists(tempPath));
    storageCheckpoint("read recovered send history");
    const auto history = reopened.recentSendHistory(10);
    std::cerr << "native_storage_tests:   recovered history size=" << history.size() << std::endl;
    assert(history.size() == 1);
    assert(history[0].content == "AA55");

    std::filesystem::remove_all(path);
}

svm::native_storage::RawIoEvent rawEvent(std::string direction, std::vector<std::uint8_t> payload) {
    svm::native_storage::RawIoEvent event;
    event.sessionId = "session-1";
    event.direction = std::move(direction);
    event.timestampUtc = "2026-06-12T10:00:00Z";
    event.endpoint = "COM3";
    event.payload = std::move(payload);
    return event;
}

void idCountersPersistAcrossReopen() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        assert(store.appendRawEvent(rawEvent("Tx", {0x01})));
    }

    auto reopened = openStore(path);
    assert(reopened.appendRawEvent(rawEvent("Rx", {0x02})));
    const auto recent = reopened.recentRawEvents(2);
    assert(recent.size() == 2);
    assert(recent[0].id == 2);
    assert(recent[1].id == 1);

    std::filesystem::remove_all(path);
}

void missingOrCorruptIdCountersAreRebuilt() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        assert(store.appendRawEvent(rawEvent("Tx", {0x01})));
    }

    std::filesystem::remove(path / "id_counters.svmr");
    {
        auto reopened = openStore(path);
        assert(reopened.appendRawEvent(rawEvent("Rx", {0x02})));
        const auto recent = reopened.recentRawEvents(2);
        assert(recent.size() == 2);
        assert(recent[0].id == 2);
        assert(recent[1].id == 1);
    }

    {
        std::ofstream counters(path / "id_counters.svmr", std::ios::binary | std::ios::trunc);
        counters << "broken counters";
    }
    {
        auto reopened = openStore(path);
        assert(reopened.appendRawEvent(rawEvent("Rx", {0x03})));
        const auto recent = reopened.recentRawEvents(3);
        assert(recent.size() == 3);
        assert(recent[0].id == 3);
        assert(recent[1].id == 2);
        assert(recent[2].id == 1);
    }

    std::filesystem::remove_all(path);
}

void presetRecordIdsAdvancePersistedCounters() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        svm::native_storage::RawIoEvent event = rawEvent("Tx", {0x64});
        event.id = 100;
        assert(store.appendRawEvent(event));
    }

    auto reopened = openStore(path);
    assert(reopened.appendRawEvent(rawEvent("Rx", {0x65})));
    const auto recent = reopened.recentRawEvents(2);
    assert(recent.size() == 2);
    assert(recent[0].id == 101);
    assert(recent[1].id == 100);

    std::filesystem::remove_all(path);
}

void rawEventRetentionKeepsRecentEventsAndContinuesIds() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        store.setRawEventRetentionLimit(900, 520);
        for (int index = 0; index < 12; ++index) {
            std::vector<std::uint8_t> payload(96, static_cast<std::uint8_t>(index));
            assert(store.appendRawEvent(rawEvent(index % 2 == 0 ? "Tx" : "Rx", std::move(payload))));
        }

        const std::int64_t retainedCount = store.rawEventCount();
        assert(retainedCount > 0);
        assert(retainedCount < 12);
        const auto recent = store.recentRawEvents(20);
        assert(recent.front().id == 12);
        assert(recent.front().payload.front() == 11);
    }

    auto reopened = openStore(path);
    reopened.setRawEventRetentionLimit(900, 520);
    assert(reopened.appendRawEvent(rawEvent("Rx", {0x13})));
    const auto recent = reopened.recentRawEvents(20);
    assert(!recent.empty());
    assert(recent.front().id == 13);
    assert(recent.front().payload.front() == 0x13);

    std::filesystem::remove_all(path);
}

std::vector<std::uint8_t> specialPayload(int seed) {
    std::vector<std::uint8_t> payload;
    payload.reserve(120);
    for (int index = 0; index < 12; ++index) {
        payload.push_back(static_cast<std::uint8_t>(seed + index));
        payload.push_back('\n');
        payload.push_back('\r');
        payload.push_back('|');
        payload.push_back(':');
        payload.push_back('0');
        payload.push_back('9');
        payload.push_back(0x00);
        payload.push_back(0xFF);
    }
    return payload;
}

void rawEventRetentionKeepsBinaryPayloadBoundaries() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        store.setRawEventRetentionLimit(900, 520);
        for (int index = 0; index < 12; ++index) {
            assert(store.appendRawEvent(rawEvent(index % 2 == 0 ? "Tx" : "Rx", specialPayload(index))));
        }

        assert(store.rawEventCount() > 0);
        assert(store.rawEventCount() < 12);
        const auto recent = store.recentRawEvents(20);
        assert(!recent.empty());
        assert(recent.front().id == 12);
        assert(recent.front().payload == specialPayload(11));
    }

    auto reopened = openStore(path);
    reopened.setRawEventRetentionLimit(900, 520);
    const auto recent = reopened.recentRawEvents(20);
    assert(!recent.empty());
    assert(recent.front().id == 12);
    assert(recent.front().payload == specialPayload(11));

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

    svm::native_storage::SendHistoryEntry third;
    third.content = "AA55";
    third.payloadMode = 1;
    third.lineEnding = 0;
    third.textEncodingCodePage = 65001;
    third.sentAtUtc = "2026-06-12T10:03:00Z";
    assert(store.saveSendHistory(third, 2));
    const auto trimmedHistory = store.recentSendHistory(10);
    assert(trimmedHistory.size() == 2);
    assert(trimmedHistory[0].content == "AA55");
    assert(trimmedHistory[1].content == "010300000001");

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

    svm::native_storage::SerialProfile auxProfile;
    auxProfile.name = "aux";
    auxProfile.portName = "COM11";
    auxProfile.baudRate = 9600;
    assert(store.saveSerialProfile(auxProfile));

    svm::native_storage::SerialProfile replacementProfile = profile;
    replacementProfile.portName = "COM12";
    replacementProfile.baudRate = 115200;
    assert(store.saveSerialProfile(replacementProfile));
    latest = store.latestSerialProfile();
    assert(latest.has_value());
    assert(latest->name == "default");
    assert(latest->portName == "COM12");
    assert(latest->baudRate == 115200);

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
    preferences.autoReconnect = true;
    preferences.timedSendEnabled = true;
    preferences.timedSendPeriodMs = 250;
    preferences.fileSendDelayMs = 20;
    preferences.logVisibleCharLimit = 500000;
    preferences.rawEventRetentionLimitMb = 500;
    preferences.quickSendSlots = {"01 03 00 00 00 01", "AT+RST", "", "", "", "", "", "", "", "AA 55"};
    preferences.windowLeft = 20;
    preferences.windowTop = 30;
    preferences.windowWidth = 1280;
    preferences.windowHeight = 820;
    preferences.updatedAtUtc = "2026-06-13T00:00:00Z";
    assert(store.saveUiPreferences(preferences));

    svm::native_storage::UiPreferences auxiliaryPreferences;
    auxiliaryPreferences.name = "aux";
    auxiliaryPreferences.logThemeIndex = 1;
    auxiliaryPreferences.updatedAtUtc = "2026-06-13T00:10:00Z";
    assert(store.saveUiPreferences(auxiliaryPreferences));

    svm::native_storage::UiPreferences replacementPreferences = preferences;
    replacementPreferences.logThemeIndex = 3;
    replacementPreferences.logVisibleCharLimit = 750000;
    replacementPreferences.updatedAtUtc = "2026-06-13T00:20:00Z";
    assert(store.saveUiPreferences(replacementPreferences));

    auto reopened = openStore(path);
    const auto loaded = reopened.latestUiPreferences();
    assert(loaded.has_value());
    assert(loaded->name == "default");
    assert(loaded->logThemeIndex == 3);
    assert(loaded->logFormat == 4);
    assert(loaded->logEncodingCodePage == 936);
    assert(!loaded->showLogTimestamps);
    assert(loaded->sendPayloadMode == 1);
    assert(loaded->sendTextEncodingCodePage == 936);
    assert(loaded->sendLineEnding == 3);
    assert(loaded->autoReconnect);
    assert(loaded->timedSendEnabled);
    assert(loaded->timedSendPeriodMs == 250);
    assert(loaded->fileSendDelayMs == 20);
    assert(loaded->logVisibleCharLimit == 750000);
    assert(loaded->rawEventRetentionLimitMb == 500);
    assert(loaded->quickSendSlots.size() == 10);
    assert(loaded->quickSendSlots[0] == "01 03 00 00 00 01");
    assert(loaded->quickSendSlots[1] == "AT+RST");
    assert(loaded->quickSendSlots[9] == "AA 55");
    assert(loaded->windowLeft == 20);
    assert(loaded->windowTop == 30);
    assert(loaded->windowWidth == 1280);
    assert(loaded->windowHeight == 820);

    std::filesystem::remove_all(path);
}

svm::native_storage::ScanExecutionRecord scanExecution(std::string sessionId, int startAddress, int registerCount) {
    svm::native_storage::ScanExecutionRecord execution;
    execution.session.sessionId = std::move(sessionId);
    execution.session.slaveId = 1;
    execution.session.functionCode = 3;
    execution.session.startAddress = startAddress;
    execution.session.endAddress = startAddress + registerCount - 1;
    execution.session.blockSize = registerCount;
    execution.session.requestCount = 1;
    execution.session.status = "completed";
    execution.session.startedAtUtc = "2026-06-12T10:04:00Z";
    execution.session.finishedAtUtc = "2026-06-12T10:04:01Z";
    execution.session.successBlockCount = 1;

    svm::native_storage::ScanAttemptRecord attempt;
    attempt.blockIndex = 0;
    attempt.attemptIndex = 0;
    attempt.startAddress = startAddress;
    attempt.quantity = registerCount;
    attempt.status = "success";
    attempt.requestFrame = {0x01, 0x03};
    attempt.responseFrame = {0x01, 0x03};
    attempt.endpoint = "COM3";
    execution.attempts.push_back(attempt);

    for (int offset = 0; offset < registerCount; ++offset) {
        svm::native_storage::ScanObservationRecord observation;
        observation.blockIndex = 0;
        observation.attemptIndex = 0;
        observation.slaveId = 1;
        observation.functionCode = 3;
        observation.address = startAddress + offset;
        observation.value = 1000 + offset;
        observation.observedAtUtc = "2026-06-12T10:04:01Z";
        execution.observations.push_back(std::move(observation));
    }
    return execution;
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

void scanObservationReadsTargetSessionFromLargeHistory() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    for (int index = 0; index < 40; ++index) {
        assert(store.saveScanExecution(scanExecution("history-before-" + std::to_string(index), index * 100, 12)));
    }
    assert(store.saveScanExecution(scanExecution("target-scan", 40000, 24)));
    for (int index = 0; index < 40; ++index) {
        assert(store.saveScanExecution(scanExecution("history-after-" + std::to_string(index), 50000 + index * 100, 12)));
    }

    const auto attempts = store.scanAttempts("target-scan");
    assert(attempts.size() == 1);
    assert(attempts[0].startAddress == 40000);

    const auto observations = store.scanObservations("target-scan");
    assert(observations.size() == 24);
    for (std::size_t index = 0; index < observations.size(); ++index) {
        assert(observations[index].sessionId == "target-scan");
        assert(observations[index].address == 40000 + static_cast<int>(index));
        assert(observations[index].value == 1000 + static_cast<int>(index));
    }
    assert(store.scanObservations("missing-scan").empty());

    std::filesystem::remove_all(path);
}

void recentAndLatestReadsStayBoundedAcrossLargeHistory() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    for (int index = 0; index < 12; ++index) {
        assert(store.saveScanExecution(scanExecution("recent-scan-" + std::to_string(index), index * 100, 2)));
    }
    const auto recentSessions = store.recentScanSessions(3);
    assert(recentSessions.size() == 3);
    assert(recentSessions[0].sessionId == "recent-scan-11");
    assert(recentSessions[1].sessionId == "recent-scan-10");
    assert(recentSessions[2].sessionId == "recent-scan-9");
    const auto latestSession = store.latestScanSession();
    assert(latestSession.has_value());
    assert(latestSession->sessionId == "recent-scan-11");
    const auto middleSession = store.scanSession("recent-scan-5");
    assert(middleSession.has_value());
    assert(middleSession->startAddress == 500);

    for (int index = 0; index < 12; ++index) {
        svm::native_storage::MatchRunRecord run;
        run.runId = "recent-match-" + std::to_string(index);
        run.sourceScanSessionId = "recent-scan-" + std::to_string(index);
        run.targetLabel = "目标";
        run.targetValue = static_cast<double>(index);
        run.createdAtUtc = "2026-06-12T12:00:00Z";
        assert(store.saveMatchRun(run, {}));
    }
    const auto recentRuns = store.recentMatchRuns(2);
    assert(recentRuns.size() == 2);
    assert(recentRuns[0].runId == "recent-match-11");
    assert(recentRuns[1].runId == "recent-match-10");
    const auto latestRun = store.latestMatchRun();
    assert(latestRun.has_value());
    assert(latestRun->runId == "recent-match-11");
    const auto middleRun = store.matchRun("recent-match-4");
    assert(middleRun.has_value());
    assert(middleRun->targetValue == 4.0);

    for (int index = 0; index < 12; ++index) {
        svm::native_storage::ProtocolFieldRuleRecord rule;
        rule.ruleId = "recent-rule-" + std::to_string(index);
        rule.fieldName = "字段" + std::to_string(index);
        rule.candidateType = "UInt16";
        rule.wordOrder = "HighWordFirst";
        rule.byteOrder = "BigEndian";
        rule.slaveId = 1;
        rule.functionCode = 3;
        rule.startAddress = index;
        rule.registerCount = 1;
        assert(store.saveProtocolFieldRule(rule));
    }
    const auto recentRules = store.recentProtocolFieldRules(2);
    assert(recentRules.size() == 2);
    assert(recentRules[0].ruleId == "recent-rule-11");
    assert(recentRules[1].ruleId == "recent-rule-10");
    const auto middleRule = store.protocolFieldRule("recent-rule-3");
    assert(middleRule.has_value());
    assert(middleRule->fieldName == "字段3");

    for (int index = 0; index < 12; ++index) {
        svm::native_storage::RuleVerificationRunRecord run;
        run.verificationRunId = "recent-verify-" + std::to_string(index);
        run.sourceScanSessionId = "recent-scan-" + std::to_string(index);
        run.ruleCount = 1;
        run.verifiedCount = 1;
        run.createdAtUtc = "2026-06-12T13:00:00Z";

        svm::native_storage::RuleVerificationResultRecord result;
        result.ruleId = "recent-rule-" + std::to_string(index);
        result.fieldName = "字段" + std::to_string(index);
        result.candidateType = "UInt16";
        result.sourceScanSessionId = run.sourceScanSessionId;
        result.verified = true;
        result.statusText = "已验证";
        result.slaveId = 1;
        result.functionCode = 3;
        result.startAddress = index;
        result.registerCount = 1;
        assert(store.saveRuleVerificationRun(run, {result}));
    }
    const auto latestVerification = store.latestRuleVerificationRun();
    assert(latestVerification.has_value());
    assert(latestVerification->verificationRunId == "recent-verify-11");
    const auto middleVerification = store.ruleVerificationRun("recent-verify-6");
    assert(middleVerification.has_value());
    assert(middleVerification->sourceScanSessionId == "recent-scan-6");
    const auto middleResults = store.ruleVerificationResults("recent-verify-6");
    assert(middleResults.size() == 1);
    assert(middleResults[0].fieldName == "字段6");

    std::filesystem::remove_all(path);
}

void scanExecutionWithSameSessionIdReplacesPreviousRecords() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::ScanExecutionRecord first;
    first.session.sessionId = "scan-replace";
    first.session.slaveId = 1;
    first.session.functionCode = 3;
    first.session.startAddress = 10;
    first.session.endAddress = 10;
    first.session.blockSize = 1;
    first.session.requestCount = 1;
    first.session.status = "partial";
    first.session.startedAtUtc = "2026-06-12T11:00:00Z";
    first.session.finishedAtUtc = "2026-06-12T11:00:01Z";
    first.session.failedBlockCount = 1;

    svm::native_storage::ScanAttemptRecord firstAttempt;
    firstAttempt.blockIndex = 0;
    firstAttempt.attemptIndex = 0;
    firstAttempt.startAddress = 10;
    firstAttempt.quantity = 1;
    firstAttempt.status = "timeout";
    first.attempts.push_back(firstAttempt);

    svm::native_storage::ScanObservationRecord firstObservation;
    firstObservation.blockIndex = 0;
    firstObservation.attemptIndex = 0;
    firstObservation.slaveId = 1;
    firstObservation.functionCode = 3;
    firstObservation.address = 10;
    firstObservation.value = 100;
    first.observations.push_back(firstObservation);
    assert(store.saveScanExecution(first));

    svm::native_storage::ScanExecutionRecord second = first;
    second.session.status = "completed";
    second.session.successBlockCount = 1;
    second.session.failedBlockCount = 0;
    second.attempts.clear();
    second.observations.clear();

    svm::native_storage::ScanAttemptRecord secondAttempt;
    secondAttempt.blockIndex = 0;
    secondAttempt.attemptIndex = 0;
    secondAttempt.startAddress = 10;
    secondAttempt.quantity = 1;
    secondAttempt.status = "success";
    second.attempts.push_back(secondAttempt);

    svm::native_storage::ScanObservationRecord secondObservation;
    secondObservation.blockIndex = 0;
    secondObservation.attemptIndex = 0;
    secondObservation.slaveId = 1;
    secondObservation.functionCode = 3;
    secondObservation.address = 10;
    secondObservation.value = 222;
    second.observations.push_back(secondObservation);
    assert(store.saveScanExecution(second));

    const auto sessions = store.recentScanSessions(10);
    assert(sessions.size() == 1);
    assert(sessions[0].status == "completed");
    const auto attempts = store.scanAttempts("scan-replace");
    assert(attempts.size() == 1);
    assert(attempts[0].status == "success");
    const auto observations = store.scanObservations("scan-replace");
    assert(observations.size() == 1);
    assert(observations[0].value == 222);

    std::filesystem::remove_all(path);
}

void failedScanReplacementRollsBackAllFiles() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    auto first = scanExecution("scan-transaction", 1000, 2);
    first.session.status = "old";
    first.attempts[0].status = "old-attempt";
    first.observations[0].value = 111;
    assert(store.saveScanExecution(first));

    auto replacement = scanExecution("scan-transaction", 3000, 3);
    replacement.session.status = "new";
    replacement.attempts[0].status = "new-attempt";
    replacement.observations[0].value = 999;

    setReplaceFailureInjection("scan_attempts.svmr");
    assert(!store.saveScanExecution(replacement));
    setReplaceFailureInjection(nullptr);

    const auto loaded = store.scanSession("scan-transaction");
    assert(loaded.has_value());
    assert(loaded->status == "old");
    assert(loaded->startAddress == 1000);

    const auto attempts = store.scanAttempts("scan-transaction");
    assert(attempts.size() == 1);
    assert(attempts[0].status == "old-attempt");
    assert(attempts[0].startAddress == 1000);

    const auto observations = store.scanObservations("scan-transaction");
    assert(observations.size() == 2);
    assert(observations[0].address == 1000);
    assert(observations[0].value == 111);

    auto reopened = openStore(path);
    const auto reopenedSession = reopened.scanSession("scan-transaction");
    assert(reopenedSession.has_value());
    assert(reopenedSession->status == "old");
    assert(reopened.scanAttempts("scan-transaction").size() == 1);
    assert(reopened.scanObservations("scan-transaction").size() == 2);

    std::filesystem::remove_all(path);
}

void orphanScanChildrenAreHiddenWhenSessionIsMissing() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        assert(store.saveScanExecution(scanExecution("orphan-scan", 7000, 3)));
        assert(store.scanAttempts("orphan-scan").size() == 1);
        assert(store.scanObservations("orphan-scan").size() == 3);
    }

    writeEmptyStoreFile(path / "scan_sessions.svmr");

    auto reopened = openStore(path);
    assert(!reopened.scanSession("orphan-scan").has_value());
    assert(reopened.scanAttempts("orphan-scan").empty());
    assert(reopened.scanObservations("orphan-scan").empty());

    std::filesystem::remove_all(path);
}

void matchRunWithSameRunIdReplacesPreviousCandidates() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::MatchRunRecord first;
    first.runId = "match-replace";
    first.sourceScanSessionId = "scan-1";
    first.targetLabel = "温度";
    first.targetValue = 12.34;
    first.targetUnit = "℃";
    first.createdAtUtc = "2026-06-12T12:00:00Z";

    svm::native_storage::MatchCandidateRecord candidate;
    candidate.candidateType = "UInt16";
    candidate.wordOrder = "HighWordFirst";
    candidate.byteOrder = "BigEndian";
    candidate.sourceSessionId = "scan-1";
    candidate.slaveId = 1;
    candidate.functionCode = 3;
    candidate.startAddress = 10;
    candidate.registerCount = 1;
    candidate.rawRegisters = {1234};
    candidate.engineeringValue = 12.34;
    candidate.score = 100.0;
    assert(store.saveMatchRun(first, {candidate}));
    assert(store.matchCandidates("match-replace").size() == 1);

    svm::native_storage::MatchRunRecord replacement = first;
    replacement.targetValue = 99.0;
    assert(store.saveMatchRun(replacement, {}));

    const auto loaded = store.matchRun("match-replace");
    assert(loaded.has_value());
    assert(loaded->targetValue == 99.0);
    assert(loaded->candidateCount == 0);
    assert(store.matchCandidates("match-replace").empty());

    std::filesystem::remove_all(path);
}

void failedMatchReplacementRollsBackAllFiles() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::MatchRunRecord first;
    first.runId = "match-transaction";
    first.sourceScanSessionId = "scan-1";
    first.targetLabel = "温度";
    first.targetValue = 12.34;
    first.createdAtUtc = "2026-06-12T12:00:00Z";

    svm::native_storage::MatchCandidateRecord firstCandidate;
    firstCandidate.candidateType = "UInt16";
    firstCandidate.sourceSessionId = "scan-1";
    firstCandidate.startAddress = 10;
    firstCandidate.registerCount = 1;
    firstCandidate.rawRegisters = {1234};
    firstCandidate.engineeringValue = 12.34;
    assert(store.saveMatchRun(first, {firstCandidate}));

    svm::native_storage::MatchRunRecord replacement = first;
    replacement.targetValue = 99.0;
    svm::native_storage::MatchCandidateRecord replacementCandidate = firstCandidate;
    replacementCandidate.startAddress = 20;
    replacementCandidate.engineeringValue = 99.0;

    setReplaceFailureInjection("match_candidates.svmr");
    assert(!store.saveMatchRun(replacement, {replacementCandidate}));
    setReplaceFailureInjection(nullptr);

    const auto loaded = store.matchRun("match-transaction");
    assert(loaded.has_value());
    assert(loaded->targetValue == 12.34);
    assert(loaded->candidateCount == 1);
    const auto candidates = store.matchCandidates("match-transaction");
    assert(candidates.size() == 1);
    assert(candidates[0].startAddress == 10);
    assert(candidates[0].engineeringValue == 12.34);

    auto reopened = openStore(path);
    const auto reopenedRun = reopened.matchRun("match-transaction");
    assert(reopenedRun.has_value());
    assert(reopenedRun->targetValue == 12.34);
    assert(reopened.matchCandidates("match-transaction").size() == 1);

    std::filesystem::remove_all(path);
}

void orphanMatchCandidatesAreHiddenWhenRunIsMissing() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        svm::native_storage::MatchRunRecord run;
        run.runId = "orphan-match";
        run.sourceScanSessionId = "scan-1";
        run.targetLabel = "温度";
        run.targetValue = 12.34;
        run.createdAtUtc = "2026-06-12T12:00:00Z";

        svm::native_storage::MatchCandidateRecord candidate;
        candidate.candidateType = "UInt16";
        candidate.sourceSessionId = "scan-1";
        candidate.startAddress = 10;
        candidate.registerCount = 1;
        candidate.rawRegisters = {1234};
        assert(store.saveMatchRun(run, {candidate}));
        assert(store.matchCandidates("orphan-match").size() == 1);
    }

    writeEmptyStoreFile(path / "match_runs.svmr");

    auto reopened = openStore(path);
    assert(!reopened.matchRun("orphan-match").has_value());
    assert(reopened.matchCandidates("orphan-match").empty());

    std::filesystem::remove_all(path);
}

void ruleVerificationWithSameRunIdReplacesPreviousResults() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::RuleVerificationRunRecord first;
    first.verificationRunId = "verify-replace";
    first.sourceScanSessionId = "scan-1";
    first.ruleCount = 1;
    first.verifiedCount = 1;
    first.createdAtUtc = "2026-06-12T12:10:00Z";

    svm::native_storage::RuleVerificationResultRecord result;
    result.ruleId = "rule-1";
    result.fieldName = "温度";
    result.candidateType = "UInt16";
    result.sourceScanSessionId = "scan-1";
    result.verified = true;
    result.statusText = "已验证";
    result.slaveId = 1;
    result.functionCode = 3;
    result.startAddress = 10;
    result.registerCount = 1;
    result.rawRegisters = {1234};
    assert(store.saveRuleVerificationRun(first, {result}));
    assert(store.ruleVerificationResults("verify-replace").size() == 1);

    svm::native_storage::RuleVerificationRunRecord replacement = first;
    replacement.verifiedCount = 0;
    replacement.missingCount = 1;
    assert(store.saveRuleVerificationRun(replacement, {}));

    const auto loaded = store.ruleVerificationRun("verify-replace");
    assert(loaded.has_value());
    assert(loaded->verifiedCount == 0);
    assert(loaded->missingCount == 1);
    assert(store.ruleVerificationResults("verify-replace").empty());

    std::filesystem::remove_all(path);
}

void failedRuleVerificationReplacementRollsBackAllFiles() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    svm::native_storage::RuleVerificationRunRecord first;
    first.verificationRunId = "verify-transaction";
    first.sourceScanSessionId = "scan-1";
    first.ruleCount = 1;
    first.verifiedCount = 1;
    first.createdAtUtc = "2026-06-12T12:10:00Z";

    svm::native_storage::RuleVerificationResultRecord firstResult;
    firstResult.ruleId = "rule-1";
    firstResult.fieldName = "温度";
    firstResult.candidateType = "UInt16";
    firstResult.sourceScanSessionId = "scan-1";
    firstResult.verified = true;
    firstResult.statusText = "已验证";
    firstResult.startAddress = 10;
    firstResult.rawRegisters = {1234};
    assert(store.saveRuleVerificationRun(first, {firstResult}));

    svm::native_storage::RuleVerificationRunRecord replacement = first;
    replacement.verifiedCount = 0;
    replacement.missingCount = 1;
    svm::native_storage::RuleVerificationResultRecord replacementResult = firstResult;
    replacementResult.verified = false;
    replacementResult.statusText = "未匹配";
    replacementResult.startAddress = 20;

    setReplaceFailureInjection("rule_verification_results.svmr");
    assert(!store.saveRuleVerificationRun(replacement, {replacementResult}));
    setReplaceFailureInjection(nullptr);

    const auto loaded = store.ruleVerificationRun("verify-transaction");
    assert(loaded.has_value());
    assert(loaded->verifiedCount == 1);
    assert(loaded->missingCount == 0);
    const auto results = store.ruleVerificationResults("verify-transaction");
    assert(results.size() == 1);
    assert(results[0].verified);
    assert(results[0].statusText == "已验证");
    assert(results[0].startAddress == 10);

    auto reopened = openStore(path);
    const auto reopenedRun = reopened.ruleVerificationRun("verify-transaction");
    assert(reopenedRun.has_value());
    assert(reopenedRun->verifiedCount == 1);
    assert(reopened.ruleVerificationResults("verify-transaction").size() == 1);

    std::filesystem::remove_all(path);
}

void orphanRuleVerificationResultsAreHiddenWhenRunIsMissing() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        svm::native_storage::RuleVerificationRunRecord run;
        run.verificationRunId = "orphan-verify";
        run.sourceScanSessionId = "scan-1";
        run.ruleCount = 1;
        run.verifiedCount = 1;
        run.createdAtUtc = "2026-06-12T12:10:00Z";

        svm::native_storage::RuleVerificationResultRecord result;
        result.ruleId = "rule-1";
        result.fieldName = "温度";
        result.candidateType = "UInt16";
        result.sourceScanSessionId = "scan-1";
        result.verified = true;
        result.statusText = "已验证";
        assert(store.saveRuleVerificationRun(run, {result}));
        assert(store.ruleVerificationResults("orphan-verify").size() == 1);
    }

    writeEmptyStoreFile(path / "rule_verification_runs.svmr");

    auto reopened = openStore(path);
    assert(!reopened.ruleVerificationRun("orphan-verify").has_value());
    assert(reopened.ruleVerificationResults("orphan-verify").empty());

    std::filesystem::remove_all(path);
}

void replacementWritesKeepUnrelatedHistoryRecords() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    for (int index = 0; index < 8; ++index) {
        assert(store.saveScanExecution(scanExecution("side-scan-before-" + std::to_string(index), index * 20, 2)));
    }
    assert(store.saveScanExecution(scanExecution("hot-scan", 1000, 3)));
    for (int index = 0; index < 8; ++index) {
        assert(store.saveScanExecution(scanExecution("side-scan-after-" + std::to_string(index), 2000 + index * 20, 2)));
    }
    assert(store.saveScanExecution(scanExecution("hot-scan", 3000, 2)));

    const auto hotScan = store.scanSession("hot-scan");
    assert(hotScan.has_value());
    assert(hotScan->startAddress == 3000);
    const auto hotObservations = store.scanObservations("hot-scan");
    assert(hotObservations.size() == 2);
    assert(hotObservations[0].address == 3000);
    assert(store.scanObservations("side-scan-before-0").size() == 2);
    assert(store.scanObservations("side-scan-after-7").size() == 2);

    auto saveMatch = [&](std::string runId, double value, bool withCandidate) {
        svm::native_storage::MatchRunRecord run;
        run.runId = std::move(runId);
        run.sourceScanSessionId = "hot-scan";
        run.targetLabel = "目标";
        run.targetValue = value;
        run.createdAtUtc = "2026-06-12T12:00:00Z";

        std::vector<svm::native_storage::MatchCandidateRecord> candidates;
        if (withCandidate) {
            svm::native_storage::MatchCandidateRecord candidate;
            candidate.candidateType = "UInt16";
            candidate.wordOrder = "HighWordFirst";
            candidate.byteOrder = "BigEndian";
            candidate.sourceSessionId = "hot-scan";
            candidate.slaveId = 1;
            candidate.functionCode = 3;
            candidate.startAddress = 3000;
            candidate.registerCount = 1;
            candidate.rawRegisters = {1234};
            candidate.engineeringValue = value;
            candidate.score = 100.0;
            candidates.push_back(std::move(candidate));
        }
        return store.saveMatchRun(run, std::move(candidates));
    };

    for (int index = 0; index < 8; ++index) {
        assert(saveMatch("side-match-before-" + std::to_string(index), static_cast<double>(index), true));
    }
    assert(saveMatch("hot-match", 12.34, true));
    for (int index = 0; index < 8; ++index) {
        assert(saveMatch("side-match-after-" + std::to_string(index), 100.0 + index, true));
    }
    assert(saveMatch("hot-match", 99.0, false));

    const auto hotMatch = store.matchRun("hot-match");
    assert(hotMatch.has_value());
    assert(hotMatch->targetValue == 99.0);
    assert(hotMatch->candidateCount == 0);
    assert(store.matchCandidates("hot-match").empty());
    assert(store.matchCandidates("side-match-before-0").size() == 1);
    assert(store.matchCandidates("side-match-after-7").size() == 1);

    auto saveVerification = [&](std::string verificationRunId, int verifiedCount, bool withResult) {
        svm::native_storage::RuleVerificationRunRecord run;
        run.verificationRunId = std::move(verificationRunId);
        run.sourceScanSessionId = "hot-scan";
        run.ruleCount = 1;
        run.verifiedCount = verifiedCount;
        run.missingCount = withResult ? 0 : 1;
        run.createdAtUtc = "2026-06-12T13:00:00Z";

        std::vector<svm::native_storage::RuleVerificationResultRecord> results;
        if (withResult) {
            svm::native_storage::RuleVerificationResultRecord result;
            result.ruleId = "rule-hot";
            result.fieldName = "目标";
            result.candidateType = "UInt16";
            result.sourceScanSessionId = "hot-scan";
            result.verified = true;
            result.statusText = "已验证";
            result.slaveId = 1;
            result.functionCode = 3;
            result.startAddress = 3000;
            result.registerCount = 1;
            result.rawRegisters = {1234};
            results.push_back(std::move(result));
        }
        return store.saveRuleVerificationRun(run, std::move(results));
    };

    for (int index = 0; index < 8; ++index) {
        assert(saveVerification("side-verify-before-" + std::to_string(index), 1, true));
    }
    assert(saveVerification("hot-verify", 1, true));
    for (int index = 0; index < 8; ++index) {
        assert(saveVerification("side-verify-after-" + std::to_string(index), 1, true));
    }
    assert(saveVerification("hot-verify", 0, false));

    const auto hotVerification = store.ruleVerificationRun("hot-verify");
    assert(hotVerification.has_value());
    assert(hotVerification->verifiedCount == 0);
    assert(hotVerification->missingCount == 1);
    assert(store.ruleVerificationResults("hot-verify").empty());
    assert(store.ruleVerificationResults("side-verify-before-0").size() == 1);
    assert(store.ruleVerificationResults("side-verify-after-7").size() == 1);

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

    svm::native_storage::ProtocolFieldRuleRecord siblingRule = rule;
    siblingRule.ruleId = "rule-2";
    siblingRule.fieldName = "报警状态";
    siblingRule.startAddress = 201;
    assert(store.saveProtocolFieldRule(siblingRule));

    svm::native_storage::ProtocolFieldRuleRecord replacementRule = rule;
    replacementRule.fieldName = "运行使能";
    replacementRule.unit = "flag";
    assert(store.saveProtocolFieldRule(replacementRule));

    auto loadedRule = store.protocolFieldRule("rule-1");
    assert(loadedRule.has_value());
    assert(loadedRule->fieldName == "运行使能");
    assert(loadedRule->unit == "flag");
    assert(loadedRule->interpretationMap.find("运行允许") != std::string::npos);
    assert(store.protocolFieldRule("rule-2").has_value());

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
    assert(store.protocolFieldRule("rule-2").has_value());

    std::filesystem::remove_all(path);
}

} // namespace

int main() {
    runStorageTest("rawEventsAreBatchedAndReopened", rawEventsAreBatchedAndReopened);
    runStorageTest("replacementArtifactsAreRecoveredOnOpen", replacementArtifactsAreRecoveredOnOpen);
    runStorageTest("idCountersPersistAcrossReopen", idCountersPersistAcrossReopen);
    runStorageTest("missingOrCorruptIdCountersAreRebuilt", missingOrCorruptIdCountersAreRebuilt);
    runStorageTest("presetRecordIdsAdvancePersistedCounters", presetRecordIdsAdvancePersistedCounters);
    runStorageTest("rawEventRetentionKeepsRecentEventsAndContinuesIds", rawEventRetentionKeepsRecentEventsAndContinuesIds);
    runStorageTest("rawEventRetentionKeepsBinaryPayloadBoundaries", rawEventRetentionKeepsBinaryPayloadBoundaries);
    runStorageTest("sendHistoryAndProfilesKeepLatestRecords", sendHistoryAndProfilesKeepLatestRecords);
    runStorageTest("uiPreferencesRoundTrip", uiPreferencesRoundTrip);
    runStorageTest("scanAndMatchRecordsRoundTrip", scanAndMatchRecordsRoundTrip);
    runStorageTest("scanObservationReadsTargetSessionFromLargeHistory", scanObservationReadsTargetSessionFromLargeHistory);
    runStorageTest("recentAndLatestReadsStayBoundedAcrossLargeHistory", recentAndLatestReadsStayBoundedAcrossLargeHistory);
    runStorageTest("scanExecutionWithSameSessionIdReplacesPreviousRecords", scanExecutionWithSameSessionIdReplacesPreviousRecords);
    runStorageTest("failedScanReplacementRollsBackAllFiles", failedScanReplacementRollsBackAllFiles);
    runStorageTest("orphanScanChildrenAreHiddenWhenSessionIsMissing", orphanScanChildrenAreHiddenWhenSessionIsMissing);
    runStorageTest("matchRunWithSameRunIdReplacesPreviousCandidates", matchRunWithSameRunIdReplacesPreviousCandidates);
    runStorageTest("failedMatchReplacementRollsBackAllFiles", failedMatchReplacementRollsBackAllFiles);
    runStorageTest("orphanMatchCandidatesAreHiddenWhenRunIsMissing", orphanMatchCandidatesAreHiddenWhenRunIsMissing);
    runStorageTest("ruleVerificationWithSameRunIdReplacesPreviousResults", ruleVerificationWithSameRunIdReplacesPreviousResults);
    runStorageTest("failedRuleVerificationReplacementRollsBackAllFiles", failedRuleVerificationReplacementRollsBackAllFiles);
    runStorageTest("orphanRuleVerificationResultsAreHiddenWhenRunIsMissing", orphanRuleVerificationResultsAreHiddenWhenRunIsMissing);
    runStorageTest("replacementWritesKeepUnrelatedHistoryRecords", replacementWritesKeepUnrelatedHistoryRecords);
    runStorageTest("protocolRulesAndVerificationRoundTrip", protocolRulesAndVerificationRoundTrip);

    std::cout << "native_storage_tests passed\n";
    return 0;
}
