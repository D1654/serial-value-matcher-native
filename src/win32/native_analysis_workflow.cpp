#include "win32/native_analysis_workflow.h"

#if defined(_WIN32)

#include "win32/utf8_win32.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <utility>

namespace svm::win32 {
namespace {

std::string formatNativeAnalysisNumber(double value) {
    std::ostringstream output;
    output.precision(12);
    output << value;
    return output.str();
}

} // namespace

bool NativeObservationAddressKey::operator==(const NativeObservationAddressKey& other) const noexcept {
    return slaveId == other.slaveId
        && functionCode == other.functionCode
        && address == other.address;
}

std::size_t NativeObservationAddressKeyHash::operator()(const NativeObservationAddressKey& key) const noexcept {
    std::size_t value = static_cast<std::size_t>(key.slaveId);
    value = value * 1315423911u + static_cast<std::size_t>(key.functionCode);
    value = value * 1315423911u + static_cast<std::size_t>(key.address);
    return value;
}

core::analysis::RegisterSample nativeSampleFromObservation(const native_storage::ScanObservationRecord& observation) {
    core::analysis::RegisterSample sample;
    sample.observationId = observation.id;
    sample.sessionId = observation.sessionId;
    sample.slaveId = observation.slaveId;
    sample.functionCode = observation.functionCode;
    sample.address = observation.address;
    sample.value = static_cast<std::uint16_t>(observation.value);
    sample.blockIndex = observation.blockIndex;
    sample.attemptIndex = observation.attemptIndex;
    return sample;
}

NativeObservationAddressIndex nativeBuildObservationAddressIndex(const std::vector<native_storage::ScanObservationRecord>& observations) {
    NativeObservationAddressIndex index;
    index.reserve(observations.size());
    for (const native_storage::ScanObservationRecord& observation : observations) {
        const NativeObservationAddressKey key {
            observation.slaveId,
            observation.functionCode,
            observation.address,
        };
        auto [iterator, inserted] = index.emplace(key, &observation);
        if (!inserted && observation.id > iterator->second->id) {
            iterator->second = &observation;
        }
    }
    return index;
}

const native_storage::ScanObservationRecord* nativeFindIndexedObservation(
    const NativeObservationAddressIndex& index,
    const native_storage::ProtocolFieldRuleRecord& rule,
    int address) {
    const NativeObservationAddressKey key {
        rule.slaveId,
        rule.functionCode,
        address,
    };
    const auto iterator = index.find(key);
    return iterator == index.cend() ? nullptr : iterator->second;
}

native_storage::MatchCandidateRecord nativeCandidateRecordFromCore(
    const core::analysis::ValueMatchCandidate& candidate,
    const std::string& runId,
    int rankIndex,
    const std::string& observedAtUtc) {
    native_storage::MatchCandidateRecord record;
    record.runId = runId;
    record.rankIndex = rankIndex;
    record.candidateType = core::analysis::numericCandidateTypeName(candidate.type);
    record.wordOrder = core::analysis::wordOrderName(candidate.wordOrder);
    record.byteOrder = core::analysis::byteOrderName(candidate.byteOrder);
    record.sourceSessionId = candidate.sessionId;
    record.slaveId = candidate.slaveId;
    record.functionCode = candidate.functionCode;
    record.startAddress = candidate.startAddress;
    record.registerCount = candidate.registerCount;
    record.observationIds = candidate.observationIds;
    record.addresses = candidate.addresses;
    record.blockIndexes = candidate.blockIndexes;
    record.attemptIndexes = candidate.attemptIndexes;
    record.rawRegisters.reserve(candidate.rawRegisters.size());
    for (std::uint16_t value : candidate.rawRegisters) {
        record.rawRegisters.push_back(static_cast<int>(value));
    }
    record.decodedValue = candidate.decodedValue;
    record.scaleMultiplier = candidate.scale.multiplier;
    record.scaleOffset = candidate.scale.offset;
    record.engineeringValue = candidate.engineeringValue;
    record.delta = candidate.delta;
    record.absoluteError = candidate.absoluteError;
    record.effectiveTolerance = candidate.effectiveTolerance;
    record.score = candidate.score;
    record.observedAtUtc = observedAtUtc;
    record.evidenceText = candidate.evidenceText;
    return record;
}

std::wstring nativeCandidateDisplayText(const native_storage::MatchCandidateRecord& candidate) {
    std::wostringstream output;
    output << L"#" << (candidate.rankIndex + 1)
           << L" " << utf8ToWide(candidate.candidateType)
           << L" @" << candidate.startAddress
           << L"=" << utf8ToWide(formatNativeAnalysisNumber(candidate.engineeringValue))
           << L" score " << utf8ToWide(formatNativeAnalysisNumber(candidate.score));
    return output.str();
}

std::wstring nativeRuleDisplayName(const native_storage::MatchCandidateRecord& candidate, const std::wstring& targetName) {
    if (!targetName.empty()) {
        return targetName;
    }
    return utf8ToWide(candidate.candidateType) + L"@" + std::to_wstring(candidate.startAddress);
}

core::report::RuleVerificationRun nativeReportRunFromRecord(const native_storage::RuleVerificationRunRecord& record) {
    core::report::RuleVerificationRun run;
    run.verificationRunId = record.verificationRunId;
    run.sourceScanSessionId = record.sourceScanSessionId;
    run.ruleCount = record.ruleCount;
    run.verifiedCount = record.verifiedCount;
    run.missingCount = record.missingCount;
    run.unsupportedCount = record.unsupportedCount;
    run.createdAtText = record.createdAtUtc;
    return run;
}

core::report::RuleVerificationResult nativeReportResultFromRecord(const native_storage::RuleVerificationResultRecord& record) {
    core::report::RuleVerificationResult result;
    result.fieldName = record.fieldName;
    result.unit = record.unit;
    result.verified = record.verified;
    result.statusText = record.statusText;
    result.slaveId = record.slaveId;
    result.functionCode = record.functionCode;
    result.startAddress = record.startAddress;
    result.registerCount = record.registerCount;
    result.observationIds = record.observationIds;
    result.rawRegisters = record.rawRegisters;
    result.engineeringValue = record.engineeringValue;
    result.interpretationText = record.interpretationText;
    result.evidenceText = record.evidenceText;
    return result;
}

std::string nativeRenderRuleVerificationMarkdownReport(
    const native_storage::RuleVerificationRunRecord& run,
    const std::vector<native_storage::RuleVerificationResultRecord>& results) {
    const core::report::RuleVerificationRun reportRun = nativeReportRunFromRecord(run);
    std::vector<core::report::RuleVerificationResult> reportResults;
    reportResults.reserve(results.size());
    for (const native_storage::RuleVerificationResultRecord& record : results) {
        reportResults.push_back(nativeReportResultFromRecord(record));
    }
    return core::report::renderRuleVerificationMarkdownReport(reportRun, reportResults);
}

NativeRuleVerificationBuildResult nativeBuildRuleVerificationResult(
    const native_storage::ScanSessionRecord& session,
    const std::vector<native_storage::ProtocolFieldRuleRecord>& rules,
    const std::vector<native_storage::ScanObservationRecord>& observations,
    const std::string& verificationRunId,
    const std::string& createdAtUtc) {
    NativeRuleVerificationBuildResult build;
    build.run.verificationRunId = verificationRunId;
    build.run.sourceScanSessionId = session.sessionId;
    build.run.ruleCount = static_cast<int>(rules.size());
    build.run.createdAtUtc = createdAtUtc;
    build.results.reserve(rules.size());

    const NativeObservationAddressIndex observationIndex = nativeBuildObservationAddressIndex(observations);
    for (const native_storage::ProtocolFieldRuleRecord& rule : rules) {
        native_storage::RuleVerificationResultRecord result;
        result.verificationRunId = build.run.verificationRunId;
        result.ruleId = rule.ruleId;
        result.fieldName = rule.fieldName;
        result.unit = rule.unit;
        result.candidateType = rule.candidateType;
        result.sourceScanSessionId = session.sessionId;
        result.slaveId = rule.slaveId;
        result.functionCode = rule.functionCode;
        result.startAddress = rule.startAddress;
        result.registerCount = rule.registerCount;

        std::vector<std::uint16_t> registers;
        registers.reserve(static_cast<std::size_t>(std::max(0, rule.registerCount)));
        bool missing = false;
        int missingAddress = rule.startAddress;
        for (int offset = 0; offset < rule.registerCount; ++offset) {
            const int address = rule.startAddress + offset;
            const native_storage::ScanObservationRecord* observation = nativeFindIndexedObservation(observationIndex, rule, address);
            if (observation == nullptr) {
                missing = true;
                missingAddress = address;
                break;
            }
            result.observationIds.push_back(observation->id);
            result.rawRegisters.push_back(observation->value);
            result.observedAtUtc = observation->observedAtUtc;
            registers.push_back(static_cast<std::uint16_t>(observation->value));
        }

        if (missing || registers.empty()) {
            result.verified = false;
            result.statusText = wideToUtf8(std::wstring(L"\u7F3A\u5C11\u5730\u5740 ")
                + std::to_wstring(missingAddress)
                + L" \u7684\u89C2\u6D4B\uFF0C\u65E0\u6CD5\u9A8C\u8BC1\u3002");
            result.evidenceText = result.statusText;
            ++build.run.missingCount;
            build.results.push_back(std::move(result));
            continue;
        }

        const auto decoded = core::analysis::decodeNumericValue(rule.candidateType, rule.wordOrder, rule.byteOrder, registers);
        if (!decoded.has_value()) {
            result.verified = false;
            result.statusText = wideToUtf8(L"\u89E3\u7801\u5931\u8D25\uFF1A\u89C4\u5219\u7C7B\u578B\u4E0E\u5BC4\u5B58\u5668\u6570\u91CF\u4E0D\u5339\u914D\u3002");
            result.evidenceText = result.statusText;
            ++build.run.unsupportedCount;
            build.results.push_back(std::move(result));
            continue;
        }

        result.verified = true;
        result.statusText = wideToUtf8(L"\u5DF2\u9A8C\u8BC1");
        result.decodedValue = *decoded;
        result.engineeringValue = *decoded * rule.scaleMultiplier + rule.scaleOffset;
        if (rule.candidateType == "BitFlags" && !registers.empty()) {
            result.interpretationText = core::analysis::bitFlagInterpretationText(rule.interpretationMap, registers.front());
        } else if (rule.candidateType == "EnumMap") {
            result.interpretationText = core::analysis::enumMapInterpretationText(rule.interpretationMap, static_cast<int>(*decoded));
        }
        result.evidenceText = wideToUtf8(std::wstring(L"\u5B57\u6BB5\u9A8C\u8BC1\u6210\u529F\uFF1A")
            + utf8ToWide(rule.fieldName)
            + L"\uFF0C\u6765\u81EA\u626B\u63CF "
            + utf8ToWide(session.sessionId)
            + L"\u3002");
        ++build.run.verifiedCount;
        build.results.push_back(std::move(result));
    }

    return build;
}

} // namespace svm::win32

#endif
