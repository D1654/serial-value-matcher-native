#pragma once

#include "native_storage/native_session_store.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace svm::win32 {

class NativeCandidateCacheState final {
public:
    void setLatestMatchRunId(std::string runId);
    const std::string& latestMatchRunId() const noexcept;
    bool hasLatestMatchRunId() const noexcept;

    void setLatestVerificationRunId(std::string runId);
    const std::string& latestVerificationRunId() const noexcept;
    bool hasLatestVerificationRunId() const noexcept;

    void clearCandidateCache();
    bool isCandidateCacheFor(std::string_view runId) const noexcept;
    void setCandidateCache(std::string runId, std::vector<native_storage::MatchCandidateRecord> candidates);

    const std::vector<native_storage::MatchCandidateRecord>& candidates() const noexcept;
    bool candidatesEmpty() const noexcept;
    std::optional<native_storage::MatchCandidateRecord> candidateById(std::int64_t candidateId) const;
    std::optional<native_storage::MatchCandidateRecord> defaultCandidate() const;

private:
    std::string latestMatchRunId_;
    std::string cachedCandidateRunId_;
    std::string latestVerificationRunId_;
    std::vector<native_storage::MatchCandidateRecord> candidateRecords_;
};

} // namespace svm::win32
