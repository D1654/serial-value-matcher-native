#include "win32/native_candidate_cache_state.h"

#include <utility>

namespace svm::win32 {

void NativeCandidateCacheState::setLatestMatchRunId(std::string runId) {
    latestMatchRunId_ = std::move(runId);
}

const std::string& NativeCandidateCacheState::latestMatchRunId() const noexcept {
    return latestMatchRunId_;
}

bool NativeCandidateCacheState::hasLatestMatchRunId() const noexcept {
    return !latestMatchRunId_.empty();
}

void NativeCandidateCacheState::setLatestVerificationRunId(std::string runId) {
    latestVerificationRunId_ = std::move(runId);
}

const std::string& NativeCandidateCacheState::latestVerificationRunId() const noexcept {
    return latestVerificationRunId_;
}

bool NativeCandidateCacheState::hasLatestVerificationRunId() const noexcept {
    return !latestVerificationRunId_.empty();
}

void NativeCandidateCacheState::clearCandidateCache() {
    cachedCandidateRunId_.clear();
    candidateRecords_.clear();
}

bool NativeCandidateCacheState::isCandidateCacheFor(std::string_view runId) const noexcept {
    return !runId.empty() && cachedCandidateRunId_ == runId;
}

void NativeCandidateCacheState::setCandidateCache(std::string runId, std::vector<native_storage::MatchCandidateRecord> candidates) {
    cachedCandidateRunId_ = std::move(runId);
    candidateRecords_ = std::move(candidates);
}

const std::vector<native_storage::MatchCandidateRecord>& NativeCandidateCacheState::candidates() const noexcept {
    return candidateRecords_;
}

bool NativeCandidateCacheState::candidatesEmpty() const noexcept {
    return candidateRecords_.empty();
}

std::optional<native_storage::MatchCandidateRecord> NativeCandidateCacheState::candidateById(std::int64_t candidateId) const {
    if (candidateId <= 0) {
        return std::nullopt;
    }
    for (const native_storage::MatchCandidateRecord& candidate : candidateRecords_) {
        if (candidate.id == candidateId) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<native_storage::MatchCandidateRecord> NativeCandidateCacheState::defaultCandidate() const {
    if (candidateRecords_.empty()) {
        return std::nullopt;
    }
    return candidateRecords_.front();
}

} // namespace svm::win32
