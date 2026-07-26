#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace svm::report {

struct EvidenceBundleKeyValue {
    std::string key;
    std::string value;
};

struct EvidenceBundleRawEvent {
    std::int64_t id = 0;
    std::string sessionId;
    std::string direction;
    std::string timestampUtc;
    std::string endpoint;
    std::vector<std::uint8_t> payload;
    std::string operation;
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    std::string status;
    std::string deadlineStatus;
    std::uint64_t byteCount = 0;
    std::string errorCategory;
    std::uint32_t nativeCode = 0;
    std::uint32_t commErrorMask = 0;
    std::optional<std::size_t> inputQueueBytes;
    std::optional<std::size_t> outputQueueBytes;
};

struct EvidenceBundleInput {
    std::string title = "SerialValueMatcher Native evidence bundle";
    std::string generatedAtUtc;
    std::vector<EvidenceBundleKeyValue> appVersion;
    std::vector<EvidenceBundleKeyValue> scanSettings;
    std::vector<EvidenceBundleKeyValue> reportMetadata;
    std::vector<EvidenceBundleRawEvent> rawEvents;
    std::string ruleVerificationReportMarkdown;
};

struct EvidenceBundleRedactionOptions {
    bool redactAbsolutePaths = false;
    bool redactDeviceIdentifiers = false;
    bool redactRawPayload = false;
};

struct EvidenceBundleWriteOptions {
    // Must name a new dedicated child of an existing parent directory.
    std::filesystem::path outputDirectory;
    EvidenceBundleRedactionOptions redaction;
    bool includeRawEvents = true;
};

struct EvidenceBundleWriteResult {
    bool success = false;
    std::string errorMessage;
    std::vector<std::filesystem::path> writtenFiles;
};

std::string renderEvidenceBundleSummary(
    const EvidenceBundleInput& input,
    const EvidenceBundleRedactionOptions& redaction,
    bool includeRawEvents = true);

std::string renderEvidenceBundleKeyValues(
    const std::vector<EvidenceBundleKeyValue>& values,
    const EvidenceBundleRedactionOptions& redaction);

std::string renderEvidenceBundleRawEvents(
    const std::vector<EvidenceBundleRawEvent>& events,
    const EvidenceBundleRedactionOptions& redaction);

EvidenceBundleWriteResult writeEvidenceBundle(
    const EvidenceBundleInput& input,
    const EvidenceBundleWriteOptions& options);

} // namespace svm::report
