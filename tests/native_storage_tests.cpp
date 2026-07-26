#include "native_storage/native_session_store.h"
#include "native_storage/native_store_files.h"
#include "native_storage/native_store_record_codec.h"
#include "native_storage/native_store_record_io.h"
#include "native_storage/session_store_port.h"

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

template <svm::storage::SessionStoreScanPort Store>
bool saveScanExecutionThroughPort(
    Store& store,
    const typename svm::storage::SessionStorePortTraits<Store>::ScanExecution& execution) {
    return store.saveScanExecution(execution);
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

void setTransactionInterruptionInjection(const char* phase) {
#if defined(_WIN32)
    _putenv_s("SVM_NATIVE_STORE_INTERRUPT_TRANSACTION", phase == nullptr ? "" : phase);
#else
    if (phase == nullptr) {
        unsetenv("SVM_NATIVE_STORE_INTERRUPT_TRANSACTION");
    } else {
        setenv("SVM_NATIVE_STORE_INTERRUPT_TRANSACTION", phase, 1);
    }
#endif
}

void writeEmptyStoreFile(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << "SVM_NATIVE_STORE_V1\n";
    assert(output);
}

void writeStoreRecords(
    const std::filesystem::path& path,
    const std::vector<svm::native_storage::NativeSessionStore::Record>& records) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output << svm::native_storage::store_io::kHeader;
    for (const auto& record : records) {
        assert(svm::native_storage::store_io::writeRecord(output, record));
    }
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
    tx.operation = "write";
    tx.requestId = 41;
    tx.generation = 6;
    tx.status = "succeeded";
    tx.deadlineStatus = "met";
    tx.byteCount = tx.payload.size();
    tx.errorCategory = "none";
    tx.inputQueueBytes = 0;
    tx.outputQueueBytes = 2;

    svm::native_storage::RawIoEvent rx = tx;
    rx.direction = "Rx";
    rx.payload = {0x01, 0x03, 0x02, 0x12, 0x34};
    rx.operation = "read";
    rx.requestId = 42;
    rx.byteCount = rx.payload.size();
    rx.commErrorMask = 0x08;
    rx.inputQueueBytes = 5;
    rx.outputQueueBytes = 0;

    assert(store.appendRawEvents({tx, rx}));
    assert(store.rawEventCount() == 2);
    const auto recent = store.recentRawEvents(1);
    assert(recent.size() == 1);
    assert(recent[0].direction == "Rx");
    assert((recent[0].payload == std::vector<std::uint8_t>{0x01, 0x03, 0x02, 0x12, 0x34}));
    assert(recent[0].operation == "read");
    assert(recent[0].requestId == 42);
    assert(recent[0].generation == 6);
    assert(recent[0].status == "succeeded");
    assert(recent[0].deadlineStatus == "met");
    assert(recent[0].byteCount == 5);
    assert(recent[0].errorCategory == "none");
    assert(recent[0].nativeCode == 0);
    assert(recent[0].commErrorMask == 0x08);
    assert(recent[0].inputQueueBytes == 5);
    assert(recent[0].outputQueueBytes == 0);

    svm::native_storage::RawIoEvent metadataOnly = tx;
    metadataOnly.direction = "None";
    metadataOnly.payload.clear();
    metadataOnly.operation = "close";
    metadataOnly.requestId = 43;
    metadataOnly.status = "cancelled";
    metadataOnly.byteCount = 0;
    metadataOnly.errorCategory = "session_closed";
    assert(store.appendRawEvent(metadataOnly));
    const auto latest = store.recentRawEvents(1);
    assert(latest.size() == 1);
    assert(latest[0].payload.empty());
    assert(latest[0].operation == "close");
    assert(latest[0].requestId == 43);

    auto reopened = openStore(path);
    assert(reopened.rawEventCount() == 3);

    std::filesystem::remove_all(path);
}

void legacyRawEventsLoadWithEmptyOperationMetadata() {
    const svm::native_storage::NativeSessionStore::Record legacy{
        "7",
        "legacy-session",
        "Tx",
        "2026-06-12T10:00:00Z",
        "COM3",
        std::string("\x01\x02", 2),
    };
    const auto event = svm::native_storage::store_records::rawEventFromRecord(legacy);
    assert(event.id == 7);
    assert(event.direction == "Tx");
    assert(event.payload.size() == 2);
    assert(event.operation.empty());
    assert(event.requestId == 0);
    assert(event.generation == 0);
    assert(event.status.empty());
    assert(!event.inputQueueBytes.has_value());
    assert(!event.outputQueueBytes.has_value());
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

void interruptedReplacementRollsBackLiveFileToBackup() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        svm::native_storage::SendHistoryEntry entry;
        entry.content = "OLD";
        entry.payloadMode = 1;
        entry.sentAtUtc = "2026-06-12T10:01:00Z";
        assert(store.saveSendHistory(entry, 10));
    }

    const auto historyPath = path / "send_history.svmr";
    const auto backupPath = path / "send_history.svmr.bak";
    const auto tempPath = path / "send_history.svmr.tmp";
    std::error_code error;
    std::filesystem::rename(historyPath, backupPath, error);
    assert(!error);
    writeStoreRecords(historyPath, {{
        "99",
        "NEW",
        "1",
        "0",
        "2026-06-12T10:01:01Z",
        "65001",
    }});
    writeEmptyStoreFile(tempPath);

    auto reopened = openStore(path);
    const auto history = reopened.recentSendHistory(10);
    assert(history.size() == 1);
    assert(history[0].content == "OLD");
    assert(!pathExists(backupPath));
    assert(!pathExists(tempPath));

    std::filesystem::remove_all(path);
}

void truncatedAppendTailIsIsolatedOnOpen() {
    const auto path = temporaryStorePath();
    {
        auto store = openStore(path);
        svm::native_storage::RawIoEvent first;
        first.sessionId = "tail-recovery";
        first.direction = "Tx";
        first.timestampUtc = "2026-06-12T10:02:00Z";
        first.endpoint = "COM4";
        first.payload = {0x01};

        svm::native_storage::RawIoEvent second = first;
        second.direction = "Rx";
        second.payload = {0x02};
        assert(store.appendRawEvents({first, second}));
        assert(store.rawEventCount() == 2);
    }

    const auto rawPath = path / "raw_io_events.svmr";
    {
        std::ofstream output(rawPath, std::ios::binary | std::ios::app);
        assert(output);
        output << "6|2:9913:tail-recovery";
        assert(output);
    }

    svm::native_storage::NativeSessionStore reopened;
    assert(reopened.open(path));
    assert(reopened.rawEventCount() == 2);
    const auto recent = reopened.recentRawEvents(2);
    assert(recent.size() == 2);
    assert(recent[0].direction == "Rx");
    assert(reopened.lastRecoveryText().find("raw_io_events.svmr") != std::string::npos);
    assert(pathExists(rawPath.string() + ".orphan"));

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

void recentRawEventsChronologicalKeepsRecentEventsInOriginalOrder() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    assert(store.appendRawEvent(rawEvent("Tx", {0x01})));
    assert(store.appendRawEvent(rawEvent("Rx", {0x02})));
    assert(store.appendRawEvent(rawEvent("Tx", {0x03})));

    const auto events = store.recentRawEventsChronological(2);

    assert(events.size() == 2);
    assert(events[0].direction == "Rx");
    assert((events[0].payload == std::vector<std::uint8_t>{0x02}));
    assert(events[1].direction == "Tx");
    assert((events[1].payload == std::vector<std::uint8_t>{0x03}));

    std::filesystem::remove_all(path);
}

void auditRawEventsPreserveDirectionAndPayload() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    assert(store.appendRawEvent(rawEvent(
        "Audit",
        {'d', 'a', 'n', 'g', 'e', 'r', 'o', 'u', 's', '_', 'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n'})));

    const auto events = store.recentRawEventsChronological(1);

    assert(events.size() == 1);
    assert(events[0].direction == "Audit");
    assert((events[0].payload == std::vector<std::uint8_t>{
        'd', 'a', 'n', 'g', 'e', 'r', 'o', 'u', 's', '_', 'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n'}));

    std::filesystem::remove_all(path);
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
    preferences.workbenchHeight = 260;
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
    replacementPreferences.workbenchHeight = 300;
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
    assert(loaded->workbenchHeight == 300);
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

void narrowSessionStorePortKeepsBackendFilesSeparated() {
    using Store = svm::native_storage::NativeSessionStore;
    using Traits = svm::storage::SessionStorePortTraits<Store>;
    static_assert(svm::storage::SessionStorePort<Store>);
    static_assert(svm::storage::SessionStoreOpenStatePort<Store>);
    static_assert(svm::storage::SessionStoreRecentRawIoPort<Store>);
    static_assert(svm::storage::SessionStoreRawRetentionPort<Store>);
    static_assert(svm::storage::SessionStoreUiPreferencesPort<Store>);

    const auto descriptor = Traits::descriptor;
    assert(descriptor.backendKind == svm::storage::SessionStoreBackendKind::NativeFile);
    assert(descriptor.supportsRawIo);
    assert(descriptor.supportsRawRetention);
    assert(descriptor.supportsUiPreferences);
    assert(descriptor.supportsModbusScans);
    assert(!descriptor.exposesBackendFiles);
    assert(svm::native_storage::store_files::isStoreDataFile("raw_io_events.svmr"));
    assert(svm::native_storage::store_files::isStoreManagedFile("schema.txt"));
    assert(!svm::native_storage::store_files::isStoreManagedFile("port-contract.txt"));

    const auto path = temporaryStorePath();
    auto store = openStore(path);

    Traits::RawIoEvent event;
    event.sessionId = "port-session";
    event.direction = "Rx";
    event.timestampUtc = "2026-06-12T10:03:00Z";
    event.endpoint = "COM9";
    event.payload = {0x01, 0x03, 0x02, 0x12, 0x34};
    assert(store.appendRawEvent(event));
    assert(store.rawEventCount() == 1);

    const auto execution = scanExecution("port-scan", 400, 2);
    assert(saveScanExecutionThroughPort(store, execution));
    const auto loaded = store.scanSession("port-scan");
    assert(loaded.has_value());
    assert(loaded->startAddress == 400);
    assert(store.scanAttempts("port-scan").size() == 1);
    assert(store.scanObservations("port-scan").size() == 2);

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

void uncommittedTransactionRollsBackEveryFileOnOpen() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    auto first = scanExecution("scan-crash-rollback", 1000, 2);
    first.session.status = "old";
    first.attempts[0].status = "old-attempt";
    first.observations[0].value = 111;
    assert(store.saveScanExecution(first));

    auto replacement = scanExecution("scan-crash-rollback", 3000, 3);
    replacement.session.status = "new";
    replacement.attempts[0].status = "new-attempt";
    replacement.observations[0].value = 999;

    setTransactionInterruptionInjection("before_commit");
    assert(!store.saveScanExecution(replacement));
    setTransactionInterruptionInjection(nullptr);

    assert(pathExists(path / ".svm-store-transaction.svmr"));
    assert(!pathExists(path / ".svm-store-transaction.commit"));
    assert(pathExists(path / "scan_sessions.svmr.bak"));
    assert(pathExists(path / "scan_attempts.svmr.bak"));
    assert(pathExists(path / "scan_observations.svmr.bak"));

    auto reopened = openStore(path);
    const auto session = reopened.scanSession("scan-crash-rollback");
    assert(session.has_value());
    assert(session->status == "old");
    assert(session->startAddress == 1000);
    const auto attempts = reopened.scanAttempts("scan-crash-rollback");
    assert(attempts.size() == 1);
    assert(attempts[0].status == "old-attempt");
    assert(attempts[0].startAddress == 1000);
    const auto observations = reopened.scanObservations("scan-crash-rollback");
    assert(observations.size() == 2);
    assert(observations[0].address == 1000);
    assert(observations[0].value == 111);
    assert(!pathExists(path / ".svm-store-transaction.svmr"));
    assert(!pathExists(path / "scan_sessions.svmr.bak"));
    assert(!pathExists(path / "scan_attempts.svmr.bak"));
    assert(!pathExists(path / "scan_observations.svmr.bak"));

    std::filesystem::remove_all(path);
}

void committedTransactionWithMixedBackupCleanupRollsForwardEveryFileOnOpen() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    auto first = scanExecution("scan-crash-commit", 1000, 2);
    first.session.status = "old";
    first.attempts[0].status = "old-attempt";
    first.observations[0].value = 111;
    assert(store.saveScanExecution(first));

    auto replacement = scanExecution("scan-crash-commit", 3000, 3);
    replacement.session.status = "new";
    replacement.attempts[0].status = "new-attempt";
    replacement.observations[0].value = 999;

    setTransactionInterruptionInjection("during_cleanup");
    assert(!store.saveScanExecution(replacement));
    setTransactionInterruptionInjection(nullptr);

    assert(pathExists(path / ".svm-store-transaction.svmr"));
    assert(pathExists(path / ".svm-store-transaction.commit"));
    assert(!pathExists(path / "scan_sessions.svmr.bak"));
    assert(pathExists(path / "scan_attempts.svmr.bak"));
    assert(pathExists(path / "scan_observations.svmr.bak"));

    auto reopened = openStore(path);
    const auto session = reopened.scanSession("scan-crash-commit");
    assert(session.has_value());
    assert(session->status == "new");
    assert(session->startAddress == 3000);
    const auto attempts = reopened.scanAttempts("scan-crash-commit");
    assert(attempts.size() == 1);
    assert(attempts[0].status == "new-attempt");
    assert(attempts[0].startAddress == 3000);
    const auto observations = reopened.scanObservations("scan-crash-commit");
    assert(observations.size() == 3);
    assert(observations[0].address == 3000);
    assert(observations[0].value == 999);
    assert(!pathExists(path / ".svm-store-transaction.svmr"));
    assert(!pathExists(path / ".svm-store-transaction.commit"));
    assert(!pathExists(path / "scan_attempts.svmr.bak"));
    assert(!pathExists(path / "scan_observations.svmr.bak"));

    std::filesystem::remove_all(path);
}

void failedInitialParentChildSavesAreAtomicForRetry() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);

    const auto scan = scanExecution("initial-scan-transaction", 5000, 3);
    setReplaceFailureInjection("scan_attempts.svmr");
    assert(!store.saveScanExecution(scan));
    setReplaceFailureInjection(nullptr);
    assert(!store.scanSession(scan.session.sessionId).has_value());
    assert(store.saveScanExecution(scan));
    assert(store.scanAttempts(scan.session.sessionId).size() == 1);
    assert(store.scanObservations(scan.session.sessionId).size() == 3);

    svm::native_storage::MatchRunRecord matchRun;
    matchRun.runId = "initial-match-transaction";
    matchRun.sourceScanSessionId = scan.session.sessionId;
    matchRun.targetLabel = "temperature";
    matchRun.targetValue = 12.34;
    matchRun.createdAtUtc = "2026-06-12T12:00:00Z";
    svm::native_storage::MatchCandidateRecord candidate;
    candidate.candidateType = "UInt16";
    candidate.sourceSessionId = scan.session.sessionId;
    candidate.startAddress = 5000;
    candidate.registerCount = 1;
    candidate.rawRegisters = {1234};
    candidate.engineeringValue = 12.34;

    setReplaceFailureInjection("match_candidates.svmr");
    assert(!store.saveMatchRun(matchRun, {candidate}));
    setReplaceFailureInjection(nullptr);
    assert(!store.matchRun(matchRun.runId).has_value());
    assert(store.saveMatchRun(matchRun, {candidate}));
    assert(store.matchCandidates(matchRun.runId).size() == 1);

    svm::native_storage::RuleVerificationRunRecord verificationRun;
    verificationRun.verificationRunId = "initial-verification-transaction";
    verificationRun.sourceScanSessionId = scan.session.sessionId;
    verificationRun.ruleCount = 1;
    verificationRun.verifiedCount = 1;
    verificationRun.createdAtUtc = "2026-06-12T12:10:00Z";
    svm::native_storage::RuleVerificationResultRecord verificationResult;
    verificationResult.ruleId = "rule-1";
    verificationResult.fieldName = "temperature";
    verificationResult.candidateType = "UInt16";
    verificationResult.sourceScanSessionId = scan.session.sessionId;
    verificationResult.verified = true;
    verificationResult.statusText = "verified";
    verificationResult.startAddress = 5000;
    verificationResult.registerCount = 1;
    verificationResult.rawRegisters = {1234};

    setReplaceFailureInjection("rule_verification_results.svmr");
    assert(!store.saveRuleVerificationRun(verificationRun, {verificationResult}));
    setReplaceFailureInjection(nullptr);
    assert(!store.ruleVerificationRun(verificationRun.verificationRunId).has_value());
    assert(store.saveRuleVerificationRun(verificationRun, {verificationResult}));
    assert(store.ruleVerificationResults(verificationRun.verificationRunId).size() == 1);

    auto reopened = openStore(path);
    assert(reopened.scanAttempts(scan.session.sessionId).size() == 1);
    assert(reopened.scanObservations(scan.session.sessionId).size() == 3);
    assert(reopened.matchCandidates(matchRun.runId).size() == 1);
    assert(reopened.ruleVerificationResults(verificationRun.verificationRunId).size() == 1);

    std::filesystem::remove_all(path);
}

void legacyBackupDoesNotBlockGroupedSaveInSameProcess() {
    const auto path = temporaryStorePath();
    auto store = openStore(path);
    assert(store.saveScanExecution(scanExecution("legacy-backup-first", 100, 1)));

    const auto countersPath = path / "id_counters.svmr";
    const auto countersBackupPath = path / "id_counters.svmr.bak";
    std::error_code error;
    std::filesystem::copy_file(
        countersPath,
        countersBackupPath,
        std::filesystem::copy_options::overwrite_existing,
        error);
    assert(!error);

    assert(store.saveScanExecution(scanExecution("legacy-backup-second", 200, 1)));
    assert(!pathExists(countersBackupPath));
    assert(store.scanSession("legacy-backup-second").has_value());

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
    runStorageTest("legacyRawEventsLoadWithEmptyOperationMetadata", legacyRawEventsLoadWithEmptyOperationMetadata);
    runStorageTest("recentRawEventsChronologicalKeepsRecentEventsInOriginalOrder", recentRawEventsChronologicalKeepsRecentEventsInOriginalOrder);
    runStorageTest("auditRawEventsPreserveDirectionAndPayload", auditRawEventsPreserveDirectionAndPayload);
    runStorageTest("replacementArtifactsAreRecoveredOnOpen", replacementArtifactsAreRecoveredOnOpen);
    runStorageTest("interruptedReplacementRollsBackLiveFileToBackup", interruptedReplacementRollsBackLiveFileToBackup);
    runStorageTest("truncatedAppendTailIsIsolatedOnOpen", truncatedAppendTailIsIsolatedOnOpen);
    runStorageTest("idCountersPersistAcrossReopen", idCountersPersistAcrossReopen);
    runStorageTest("missingOrCorruptIdCountersAreRebuilt", missingOrCorruptIdCountersAreRebuilt);
    runStorageTest("presetRecordIdsAdvancePersistedCounters", presetRecordIdsAdvancePersistedCounters);
    runStorageTest("rawEventRetentionKeepsRecentEventsAndContinuesIds", rawEventRetentionKeepsRecentEventsAndContinuesIds);
    runStorageTest("rawEventRetentionKeepsBinaryPayloadBoundaries", rawEventRetentionKeepsBinaryPayloadBoundaries);
    runStorageTest("sendHistoryAndProfilesKeepLatestRecords", sendHistoryAndProfilesKeepLatestRecords);
    runStorageTest("uiPreferencesRoundTrip", uiPreferencesRoundTrip);
    runStorageTest("narrowSessionStorePortKeepsBackendFilesSeparated", narrowSessionStorePortKeepsBackendFilesSeparated);
    runStorageTest("scanAndMatchRecordsRoundTrip", scanAndMatchRecordsRoundTrip);
    runStorageTest("scanObservationReadsTargetSessionFromLargeHistory", scanObservationReadsTargetSessionFromLargeHistory);
    runStorageTest("recentAndLatestReadsStayBoundedAcrossLargeHistory", recentAndLatestReadsStayBoundedAcrossLargeHistory);
    runStorageTest("scanExecutionWithSameSessionIdReplacesPreviousRecords", scanExecutionWithSameSessionIdReplacesPreviousRecords);
    runStorageTest("failedScanReplacementRollsBackAllFiles", failedScanReplacementRollsBackAllFiles);
    runStorageTest("uncommittedTransactionRollsBackEveryFileOnOpen", uncommittedTransactionRollsBackEveryFileOnOpen);
    runStorageTest("committedTransactionWithMixedBackupCleanupRollsForwardEveryFileOnOpen", committedTransactionWithMixedBackupCleanupRollsForwardEveryFileOnOpen);
    runStorageTest("failedInitialParentChildSavesAreAtomicForRetry", failedInitialParentChildSavesAreAtomicForRetry);
    runStorageTest("legacyBackupDoesNotBlockGroupedSaveInSameProcess", legacyBackupDoesNotBlockGroupedSaveInSameProcess);
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
