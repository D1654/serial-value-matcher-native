#include "win32/native_candidate_cache_state.h"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

svm::native_storage::MatchCandidateRecord candidate(std::int64_t id, std::string runId, int rank) {
    svm::native_storage::MatchCandidateRecord record;
    record.id = id;
    record.runId = std::move(runId);
    record.rankIndex = rank;
    record.candidateType = "holding-register";
    return record;
}

void runIdsAreTrackedIndependently() {
    svm::win32::NativeCandidateCacheState state;
    assert(!state.hasLatestMatchRunId());
    assert(!state.hasLatestVerificationRunId());

    state.setLatestMatchRunId("match-1");
    state.setLatestVerificationRunId("verify-1");
    assert(state.hasLatestMatchRunId());
    assert(state.latestMatchRunId() == "match-1");
    assert(state.hasLatestVerificationRunId());
    assert(state.latestVerificationRunId() == "verify-1");
}

void candidateCacheTracksRunAndCanClear() {
    svm::win32::NativeCandidateCacheState state;
    std::vector<svm::native_storage::MatchCandidateRecord> records;
    records.push_back(candidate(10, "match-1", 0));
    records.push_back(candidate(11, "match-1", 1));

    state.setCandidateCache("match-1", std::move(records));
    assert(state.isCandidateCacheFor("match-1"));
    assert(!state.isCandidateCacheFor("match-2"));
    assert(state.candidates().size() == 2);

    state.clearCandidateCache();
    assert(!state.isCandidateCacheFor("match-1"));
    assert(state.candidatesEmpty());
}

void candidateSelectionFallsBackToFirstCandidate() {
    svm::win32::NativeCandidateCacheState state;
    std::vector<svm::native_storage::MatchCandidateRecord> records;
    records.push_back(candidate(10, "match-1", 0));
    records.push_back(candidate(11, "match-1", 1));
    state.setCandidateCache("match-1", std::move(records));

    const auto selected = state.candidateById(11);
    assert(selected.has_value());
    assert(selected->id == 11);

    assert(!state.candidateById(99).has_value());
    const auto fallback = state.defaultCandidate();
    assert(fallback.has_value());
    assert(fallback->id == 10);
}

} // namespace

int main() {
    runIdsAreTrackedIndependently();
    candidateCacheTracksRunAndCanClear();
    candidateSelectionFallsBackToFirstCandidate();

    std::cout << "native_candidate_cache_state_tests passed\n";
    return 0;
}
