#pragma once

#include <QString>

#include "storage/session_store.h"

namespace svm::analysis {

struct StabilityWorkflowOptions {
    int matchRunLimit = 50;
    int minimumSampleCount = 2;
    int strongSampleCount = 4;
};

struct StabilityWorkflowResult {
    bool success = false;
    QString errorMessage;
    QString stabilityRunId;
    int sourceMatchRunCount = 0;
    int candidateObservationCount = 0;
    int stableCandidateCount = 0;
};

StabilityWorkflowResult runRecentMatchStabilityAnalysis(
    storage::SessionStore& store,
    StabilityWorkflowOptions options = {});

} // namespace svm::analysis
