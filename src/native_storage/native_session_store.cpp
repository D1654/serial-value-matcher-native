#include "native_storage/native_session_store.h"
#include "native_storage/native_store_files.h"
#include "native_storage/native_store_record_codec.h"
#include "native_storage/native_store_file_ops.h"
#include "native_storage/native_store_record_io.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace svm::native_storage {
namespace {

using store_io::RecordSpan;
using store_io::copyFilePrefix;
using store_io::copyFileTail;
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
using store_file_ops::recoverReplacementTransaction;
using store_file_ops::recoveryOrphanPath;
using store_file_ops::replaceFileWithTemp;
using store_file_ops::replacementBackupPath;
using store_file_ops::replacementTempPath;
using namespace store_files;
using namespace store_records;

bool finishOutputFile(std::ofstream& output) {
    output.flush();
    output.close();
    return static_cast<bool>(output);
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

} // namespace

bool NativeSessionStore::open(const std::filesystem::path& storeDirectory) {
    lastRecoveryText_.clear();
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

    if (!recoverReplacementTransaction(storeDirectory_, lastErrorText_)) {
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
        if (!finishOutputFile(schema)) {
            lastErrorText_ = "初始化 native 存储失败：schema.txt 写入中断。";
            return false;
        }
    }

    for (std::string_view fileName : storeFiles()) {
        const auto path = filePath(fileName);
        if (!recoverReplacementArtifacts(path, lastErrorText_)) {
            return false;
        }
        if (std::filesystem::exists(path, error)) {
            if (!recoverRecordFileTail(fileName)) {
                return false;
            }
            continue;
        }
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            lastErrorText_ = "初始化 native 存储失败：无法创建 " + path.filename().string() + "。";
            return false;
        }
        output << kHeader;
        if (!finishOutputFile(output)) {
            lastErrorText_ = "初始化 native 存储失败：无法完整创建 " + path.filename().string() + "。";
            return false;
        }
    }

    if (!recoverReplacementArtifacts(filePath(kIdCountersFile), lastErrorText_)) {
        return false;
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

std::string NativeSessionStore::lastRecoveryText() const {
    return lastRecoveryText_;
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
    const auto path = filePath(kRawEventsFile);
    if (!std::filesystem::exists(path)) {
        return 0;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lastErrorText_ = "读取 native 原始记录数量失败：无法打开 raw_io_events.svmr。";
        return 0;
    }
    if (!skipStoreHeader(input)) {
        lastErrorText_ = "读取 native 原始记录数量失败：无法读取文件头。";
        return 0;
    }

    std::string parseError;
    while (true) {
        const std::optional<RecordSpan> span = readNextRecordSpan(input, &parseError, "读取 native 原始记录数量");
        if (!parseError.empty()) {
            lastErrorText_ = parseError;
            return 0;
        }
        if (!span.has_value()) {
            break;
        }
        ++count;
    }
    lastErrorText_.clear();
    return count;
}

std::vector<RawIoEvent> NativeSessionStore::recentRawEvents(std::size_t limit) const {
    std::vector<RawIoEvent> events;
    const std::size_t safeLimit = safeRecentLimit(limit);
    std::deque<RecordSpan> retainedSpans;
    const auto path = filePath(kRawEventsFile);
    if (!std::filesystem::exists(path)) {
        return events;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lastErrorText_ = "读取 native 原始记录失败：无法打开 raw_io_events.svmr。";
        return events;
    }
    if (!skipStoreHeader(input)) {
        lastErrorText_ = "读取 native 原始记录失败：无法读取文件头。";
        return events;
    }

    std::string parseError;
    while (true) {
        const std::optional<RecordSpan> span = readNextRecordSpan(input, &parseError, "读取 native 最近原始记录");
        if (!parseError.empty()) {
            lastErrorText_ = parseError;
            return {};
        }
        if (!span.has_value()) {
            break;
        }
        appendBoundedRecent(retainedSpans, *span, safeLimit);
    }

    events.reserve(retainedSpans.size());
    for (auto iterator = retainedSpans.rbegin(); iterator != retainedSpans.rend(); ++iterator) {
        if (iterator->offset > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
            lastErrorText_ = "读取 native 原始记录失败：记录偏移超出平台限制。";
            return {};
        }
        input.clear();
        input.seekg(static_cast<std::streamoff>(iterator->offset), std::ios::beg);
        if (!input) {
            lastErrorText_ = "读取 native 原始记录失败：无法定位最近记录。";
            return {};
        }
        std::string decodeError;
        std::optional<Record> record = readNextRecordFromStream(input, &decodeError, "读取 native 原始记录");
        if (!decodeError.empty()) {
            lastErrorText_ = decodeError;
            return {};
        }
        if (!record.has_value()) {
            lastErrorText_ = "读取 native 原始记录失败：最近记录缺失。";
            return {};
        }
        events.push_back(rawEventFromRecord(*record));
    }

    lastErrorText_.clear();
    return events;
}

std::vector<RawIoEvent> NativeSessionStore::recentRawEventsChronological(std::size_t limit) const {
    std::vector<RawIoEvent> events = recentRawEvents(limit);
    std::reverse(events.begin(), events.end());
    return events;
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
    if (!finishOutputFile(output)) {
        lastErrorText_ = "写入 native 存储失败：记录落盘中断。";
        return false;
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
            std::optional<RecordSpan> span = readNextRecordSpan(input, &parseError, "压缩 native 原始记录");
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

bool NativeSessionStore::recoverRecordFileTail(std::string_view fileName) {
    const auto path = filePath(fileName);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        lastErrorText_ = "恢复 native 存储记录失败：无法检查 "
            + path.filename().string() + "：" + error.message();
        return false;
    }
    if (!exists) {
        return true;
    }

    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);
    if (error) {
        lastErrorText_ = "恢复 native 存储记录失败：无法读取 "
            + path.filename().string() + " 大小：" + error.message();
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        lastErrorText_ = "恢复 native 存储记录失败：无法打开 "
            + path.filename().string() + "。";
        return false;
    }
    if (!skipStoreHeader(input)) {
        lastErrorText_ = "恢复 native 存储记录失败：无法读取 "
            + path.filename().string() + " 文件头。";
        return false;
    }

    std::streampos lastGoodPosition = input.tellg();
    if (lastGoodPosition == std::streampos(-1)) {
        lastGoodPosition = 0;
    }
    std::uintmax_t lastGoodOffset = static_cast<std::uintmax_t>(lastGoodPosition);
    std::string parseError;
    while (true) {
        std::optional<RecordSpan> span = readNextRecordSpan(input, &parseError, "恢复 native 存储记录");
        if (!parseError.empty()) {
            break;
        }
        if (!span.has_value()) {
            return true;
        }
        lastGoodOffset = span->offset + span->size;
    }
    input.close();

    if (lastGoodOffset >= fileSize) {
        lastErrorText_ = parseError;
        return false;
    }

    const auto tempPath = replacementTempPath(path);
    const auto orphanPath = recoveryOrphanPath(path);
    std::filesystem::remove(tempPath, error);
    error.clear();
    std::filesystem::remove(orphanPath, error);
    error.clear();

    if (!copyFileTail(path, orphanPath, lastGoodOffset, &lastErrorText_, "恢复 native 存储记录")) {
        std::error_code cleanupError;
        std::filesystem::remove(orphanPath, cleanupError);
        return false;
    }
    if (!copyFilePrefix(path, tempPath, lastGoodOffset, &lastErrorText_, "恢复 native 存储记录")) {
        std::error_code cleanupError;
        std::filesystem::remove(tempPath, cleanupError);
        std::filesystem::remove(orphanPath, cleanupError);
        return false;
    }

    if (!replaceFileWithTemp(tempPath, path, lastErrorText_, "恢复 native 存储记录")) {
        std::error_code cleanupError;
        std::filesystem::remove(tempPath, cleanupError);
        return false;
    }

    if (!lastRecoveryText_.empty()) {
        lastRecoveryText_.push_back('\n');
    }
    lastRecoveryText_ += "已恢复 " + path.filename().string()
        + "：保留完整记录前缀，隔离 "
        + std::to_string(fileSize - lastGoodOffset) + " 字节孤儿记录。";
    lastErrorText_.clear();
    return true;
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
        if (!finishOutputFile(output) && !writeFailed) {
            writeFailed = true;
            writeErrorText = "重写 native 存储失败：临时文件落盘中断。";
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
        if (!finishOutputFile(output) && !writeFailed) {
            writeFailed = true;
            writeErrorText = "重写 native 存储失败：临时文件落盘中断。";
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

    if (!recoverReplacementTransaction(storeDirectory_, lastErrorText_)) {
        return false;
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

    for (const RewriteRequest& request : requests) {
        if (!recoverReplacementArtifacts(filePath(request.fileName), lastErrorText_)) {
            return false;
        }
        invalidateCachesForFile(request.fileName);
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
                    if (!finishOutputFile(output) && !requestWriteFailed) {
                        requestWriteFailed = true;
                        writeErrorText = "事务重写 native 存储失败：临时文件落盘中断。";
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
