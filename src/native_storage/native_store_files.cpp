#include "native_storage/native_store_files.h"

namespace svm::native_storage::store_files {

const std::vector<std::string_view>& storeFiles() {
    static const std::vector<std::string_view> files = {
        kRawEventsFile,
        kSendHistoryFile,
        kSerialProfilesFile,
        kUiPreferencesFile,
        kScanSessionsFile,
        kScanAttemptsFile,
        kScanObservationsFile,
        kMatchRunsFile,
        kMatchCandidatesFile,
        kProtocolRulesFile,
        kRuleVerificationRunsFile,
        kRuleVerificationResultsFile,
    };
    return files;
}

} // namespace svm::native_storage::store_files
