#pragma once

#if defined(_WIN32)

#include "core/analysis_core.h"
#include "core/report_core.h"
#include "native_storage/native_session_store.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace svm::win32 {

struct NativeObservationAddressKey {
    int slaveId = 0;
    int functionCode = 0;
    int address = 0;

    bool operator==(const NativeObservationAddressKey& other) const noexcept;
};

struct NativeObservationAddressKeyHash {
    std::size_t operator()(const NativeObservationAddressKey& key) const noexcept;
};

using NativeObservationAddressIndex = std::unordered_map<
    NativeObservationAddressKey,
    const native_storage::ScanObservationRecord*,
    NativeObservationAddressKeyHash>;

struct NativeRuleVerificationBuildResult {
    native_storage::RuleVerificationRunRecord run;
    std::vector<native_storage::RuleVerificationResultRecord> results;
};

core::analysis::RegisterSample nativeSampleFromObservation(const native_storage::ScanObservationRecord& observation);
NativeObservationAddressIndex nativeBuildObservationAddressIndex(const std::vector<native_storage::ScanObservationRecord>& observations);
const native_storage::ScanObservationRecord* nativeFindIndexedObservation(
    const NativeObservationAddressIndex& index,
    const native_storage::ProtocolFieldRuleRecord& rule,
    int address);
native_storage::MatchCandidateRecord nativeCandidateRecordFromCore(
    const core::analysis::ValueMatchCandidate& candidate,
    const std::string& runId,
    int rankIndex,
    const std::string& observedAtUtc);
std::wstring nativeCandidateDisplayText(const native_storage::MatchCandidateRecord& candidate);
std::wstring nativeRuleDisplayName(const native_storage::MatchCandidateRecord& candidate, const std::wstring& targetName);
core::report::RuleVerificationRun nativeReportRunFromRecord(const native_storage::RuleVerificationRunRecord& record);
core::report::RuleVerificationResult nativeReportResultFromRecord(const native_storage::RuleVerificationResultRecord& record);
std::string nativeRenderRuleVerificationMarkdownReport(
    const native_storage::RuleVerificationRunRecord& run,
    const std::vector<native_storage::RuleVerificationResultRecord>& results);
NativeRuleVerificationBuildResult nativeBuildRuleVerificationResult(
    const native_storage::ScanSessionRecord& session,
    const std::vector<native_storage::ProtocolFieldRuleRecord>& rules,
    const std::vector<native_storage::ScanObservationRecord>& observations,
    const std::string& verificationRunId,
    const std::string& createdAtUtc);

} // namespace svm::win32

#endif
