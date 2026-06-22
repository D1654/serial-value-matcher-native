#include "win32/native_analysis_workflow.h"

#if defined(_WIN32)

#include "win32/utf8_win32.h"

#include <cstdint>
#include <sstream>

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

} // namespace svm::win32

#endif
