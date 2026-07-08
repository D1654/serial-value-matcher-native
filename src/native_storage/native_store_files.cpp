#include "native_storage/native_store_files.h"

#include <algorithm>

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

bool isStoreDataFile(std::string_view fileName) {
    const auto& files = storeFiles();
    return std::find(files.begin(), files.end(), fileName) != files.end();
}

bool isStoreManagedFile(std::string_view fileName) {
    return fileName == kSchemaFile ||
           fileName == kIdCountersFile ||
           isStoreDataFile(fileName);
}

} // namespace svm::native_storage::store_files
