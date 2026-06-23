#include "native_storage/native_session_store.h"
#include "native_storage/native_store_files.h"
#include "native_storage/native_store_record_codec.h"

#include <algorithm>
#include <string>
#include <utility>

namespace svm::native_storage {
namespace {

using namespace store_files;
using namespace store_records;

} // namespace

void NativeSessionStore::invalidateCachesForFile(std::string_view fileName) const {
    if (fileName == kScanSessionsFile) {
        scanSessionsCacheValid_ = false;
        scanSessionsCache_.clear();
        scanAttemptsCacheValid_ = false;
        scanObservationsCacheValid_ = false;
        scanAttemptsCache_.clear();
        scanObservationsCache_.clear();
    } else if (fileName == kMatchRunsFile) {
        matchRunsCacheValid_ = false;
        matchRunsCache_.clear();
        matchCandidatesCacheValid_ = false;
        matchCandidatesCache_.clear();
    } else if (fileName == kRuleVerificationRunsFile) {
        ruleVerificationRunsCacheValid_ = false;
        ruleVerificationRunsCache_.clear();
        ruleVerificationResultsCacheValid_ = false;
        ruleVerificationResultsCache_.clear();
    } else if (fileName == kScanAttemptsFile) {
        scanAttemptsCacheValid_ = false;
        scanAttemptsCache_.clear();
    } else if (fileName == kScanObservationsFile) {
        scanObservationsCacheValid_ = false;
        scanObservationsCache_.clear();
    } else if (fileName == kMatchCandidatesFile) {
        matchCandidatesCacheValid_ = false;
        matchCandidatesCache_.clear();
    } else if (fileName == kRuleVerificationResultsFile) {
        ruleVerificationResultsCacheValid_ = false;
        ruleVerificationResultsCache_.clear();
    }
}

const std::vector<ScanSessionRecord>& NativeSessionStore::cachedScanSessions() const {
    if (scanSessionsCacheValid_) {
        return scanSessionsCache_;
    }
    scanSessionsCache_.clear();
    const bool ok = visitRecords(kScanSessionsFile, [&](const Record& record) {
        scanSessionsCache_.push_back(scanSessionFromRecord(record));
        return true;
    });
    if (ok) {
        scanSessionsCacheValid_ = true;
    } else {
        scanSessionsCache_.clear();
    }
    return scanSessionsCache_;
}

const std::vector<MatchRunRecord>& NativeSessionStore::cachedMatchRuns() const {
    if (matchRunsCacheValid_) {
        return matchRunsCache_;
    }
    matchRunsCache_.clear();
    const bool ok = visitRecords(kMatchRunsFile, [&](const Record& record) {
        matchRunsCache_.push_back(matchRunFromRecord(record));
        return true;
    });
    if (ok) {
        matchRunsCacheValid_ = true;
    } else {
        matchRunsCache_.clear();
    }
    return matchRunsCache_;
}

const std::vector<RuleVerificationRunRecord>& NativeSessionStore::cachedRuleVerificationRuns() const {
    if (ruleVerificationRunsCacheValid_) {
        return ruleVerificationRunsCache_;
    }
    ruleVerificationRunsCache_.clear();
    const bool ok = visitRecords(kRuleVerificationRunsFile, [&](const Record& record) {
        ruleVerificationRunsCache_.push_back(verificationRunFromRecord(record));
        return true;
    });
    if (ok) {
        ruleVerificationRunsCacheValid_ = true;
    } else {
        ruleVerificationRunsCache_.clear();
    }
    return ruleVerificationRunsCache_;
}

const std::vector<ScanAttemptRecord>& NativeSessionStore::cachedScanAttempts(std::string_view sessionId) const {
    if (scanAttemptsCacheValid_ && scanAttemptsCacheSessionId_ == sessionId) {
        return scanAttemptsCache_;
    }
    scanAttemptsCacheValid_ = false;
    scanAttemptsCacheSessionId_ = std::string(sessionId);
    scanAttemptsCache_.clear();
    const bool ok = visitRecords(kScanAttemptsFile, [&](const Record& record) {
        ScanAttemptRecord attempt = scanAttemptFromRecord(record);
        if (attempt.sessionId == sessionId) {
            scanAttemptsCache_.push_back(std::move(attempt));
        }
        return true;
    });
    if (!ok) {
        scanAttemptsCache_.clear();
        return scanAttemptsCache_;
    }
    std::sort(scanAttemptsCache_.begin(), scanAttemptsCache_.end(), [](const ScanAttemptRecord& left, const ScanAttemptRecord& right) {
        if (left.blockIndex != right.blockIndex) {
            return left.blockIndex < right.blockIndex;
        }
        if (left.attemptIndex != right.attemptIndex) {
            return left.attemptIndex < right.attemptIndex;
        }
        return left.id < right.id;
    });
    scanAttemptsCacheValid_ = true;
    return scanAttemptsCache_;
}

const std::vector<ScanObservationRecord>& NativeSessionStore::cachedScanObservations(std::string_view sessionId) const {
    if (scanObservationsCacheValid_ && scanObservationsCacheSessionId_ == sessionId) {
        return scanObservationsCache_;
    }
    scanObservationsCacheValid_ = false;
    scanObservationsCacheSessionId_ = std::string(sessionId);
    scanObservationsCache_.clear();
    const bool ok = visitRecords(kScanObservationsFile, [&](const Record& record) {
        ScanObservationRecord observation = scanObservationFromRecord(record);
        if (observation.sessionId == sessionId) {
            scanObservationsCache_.push_back(std::move(observation));
        }
        return true;
    });
    if (!ok) {
        scanObservationsCache_.clear();
        return scanObservationsCache_;
    }
    std::sort(scanObservationsCache_.begin(), scanObservationsCache_.end(), [](const ScanObservationRecord& left, const ScanObservationRecord& right) {
        if (left.address != right.address) {
            return left.address < right.address;
        }
        return left.id < right.id;
    });
    scanObservationsCacheValid_ = true;
    return scanObservationsCache_;
}

const std::vector<MatchCandidateRecord>& NativeSessionStore::cachedMatchCandidates(std::string_view runId) const {
    if (matchCandidatesCacheValid_ && matchCandidatesCacheRunId_ == runId) {
        return matchCandidatesCache_;
    }
    matchCandidatesCacheValid_ = false;
    matchCandidatesCacheRunId_ = std::string(runId);
    matchCandidatesCache_.clear();
    const bool ok = visitRecords(kMatchCandidatesFile, [&](const Record& record) {
        MatchCandidateRecord candidate = matchCandidateFromRecord(record);
        if (candidate.runId == runId) {
            matchCandidatesCache_.push_back(std::move(candidate));
        }
        return true;
    });
    if (!ok) {
        matchCandidatesCache_.clear();
        return matchCandidatesCache_;
    }
    std::sort(matchCandidatesCache_.begin(), matchCandidatesCache_.end(), [](const MatchCandidateRecord& left, const MatchCandidateRecord& right) {
        if (left.rankIndex != right.rankIndex) {
            return left.rankIndex < right.rankIndex;
        }
        return left.id < right.id;
    });
    matchCandidatesCacheValid_ = true;
    return matchCandidatesCache_;
}

const std::vector<RuleVerificationResultRecord>& NativeSessionStore::cachedRuleVerificationResults(std::string_view verificationRunId) const {
    if (ruleVerificationResultsCacheValid_ && ruleVerificationResultsCacheRunId_ == verificationRunId) {
        return ruleVerificationResultsCache_;
    }
    ruleVerificationResultsCacheValid_ = false;
    ruleVerificationResultsCacheRunId_ = std::string(verificationRunId);
    ruleVerificationResultsCache_.clear();
    const bool ok = visitRecords(kRuleVerificationResultsFile, [&](const Record& record) {
        RuleVerificationResultRecord result = verificationResultFromRecord(record);
        if (result.verificationRunId == verificationRunId) {
            ruleVerificationResultsCache_.push_back(std::move(result));
        }
        return true;
    });
    if (!ok) {
        ruleVerificationResultsCache_.clear();
        return ruleVerificationResultsCache_;
    }
    std::sort(ruleVerificationResultsCache_.begin(), ruleVerificationResultsCache_.end(), [](const RuleVerificationResultRecord& left, const RuleVerificationResultRecord& right) {
        return left.id < right.id;
    });
    ruleVerificationResultsCacheValid_ = true;
    return ruleVerificationResultsCache_;
}

} // namespace svm::native_storage
