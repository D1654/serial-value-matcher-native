#pragma once

#include <string_view>
#include <vector>

namespace svm::native_storage::store_files {

inline constexpr std::string_view kSchemaFile = "schema.txt";
inline constexpr std::string_view kIdCountersFile = "id_counters.svmr";
inline constexpr std::string_view kRawEventsFile = "raw_io_events.svmr";
inline constexpr std::string_view kSendHistoryFile = "send_history.svmr";
inline constexpr std::string_view kSerialProfilesFile = "serial_profiles.svmr";
inline constexpr std::string_view kUiPreferencesFile = "ui_preferences.svmr";
inline constexpr std::string_view kScanSessionsFile = "scan_sessions.svmr";
inline constexpr std::string_view kScanAttemptsFile = "scan_attempts.svmr";
inline constexpr std::string_view kScanObservationsFile = "scan_observations.svmr";
inline constexpr std::string_view kMatchRunsFile = "match_runs.svmr";
inline constexpr std::string_view kMatchCandidatesFile = "match_candidates.svmr";
inline constexpr std::string_view kProtocolRulesFile = "protocol_field_rules.svmr";
inline constexpr std::string_view kRuleVerificationRunsFile = "rule_verification_runs.svmr";
inline constexpr std::string_view kRuleVerificationResultsFile = "rule_verification_results.svmr";

const std::vector<std::string_view>& storeFiles();

} // namespace svm::native_storage::store_files
