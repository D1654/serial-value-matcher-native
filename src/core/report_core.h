#pragma once

#include "core/text.h"

#include <cstdint>
#include <vector>

namespace svm::core::report {

struct RuleVerificationRun {
    Text verificationRunId;
    Text sourceScanSessionId;
    int ruleCount = 0;
    int verifiedCount = 0;
    int missingCount = 0;
    int unsupportedCount = 0;
    Text createdAtText;
};

struct RuleVerificationResult {
    Text fieldName;
    Text unit;
    bool verified = false;
    Text statusText;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    std::vector<std::int64_t> observationIds;
    std::vector<int> rawRegisters;
    double engineeringValue = 0.0;
    Text interpretationText;
    Text evidenceText;
};

Text renderRuleVerificationMarkdownReport(
    const RuleVerificationRun& run,
    const std::vector<RuleVerificationResult>& results);

} // namespace svm::core::report
