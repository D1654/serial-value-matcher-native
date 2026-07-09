#pragma once

#if defined(_WIN32)

#include "core/analysis_core.h"
#include "core/report_core.h"
#include "native_storage/native_session_store.h"
#include "report/evidence_bundle_writer.h"

#include <optional>
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

struct NativeCandidateAnalysisBuildResult {
    bool success = false;
    std::string errorMessage;
    native_storage::MatchRunRecord run;
    std::vector<native_storage::MatchCandidateRecord> candidates;
};

struct NativeEvidenceBundleContext {
    std::string generatedAtUtc;
    std::string appVersion;
    std::string selectedPortName;
    std::vector<native_storage::RawIoEvent> rawEvents;
    std::optional<native_storage::ScanSessionRecord> latestScanSession;
    std::optional<native_storage::RuleVerificationRunRecord> latestVerificationRun;
    std::vector<native_storage::RuleVerificationResultRecord> latestVerificationResults;
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
svm::report::EvidenceBundleRawEvent nativeEvidenceRawEventFromRecord(const native_storage::RawIoEvent& record);
svm::report::EvidenceBundleInput nativeBuildEvidenceBundleInput(const NativeEvidenceBundleContext& context);
NativeRuleVerificationBuildResult nativeBuildRuleVerificationResult(
    const native_storage::ScanSessionRecord& session,
    const std::vector<native_storage::ProtocolFieldRuleRecord>& rules,
    const std::vector<native_storage::ScanObservationRecord>& observations,
    const std::string& verificationRunId,
    const std::string& createdAtUtc);
NativeCandidateAnalysisBuildResult nativeBuildCandidateAnalysisRun(
    const native_storage::ScanSessionRecord& session,
    const std::vector<native_storage::ScanObservationRecord>& observations,
    const std::string& targetLabel,
    double targetValue,
    const std::string& targetUnit,
    double toleranceAbsolute,
    const std::string& runId,
    const std::string& sampledAtUtc,
    const std::string& createdAtUtc,
    const std::string& observedAtUtc);

} // namespace svm::win32

#endif
