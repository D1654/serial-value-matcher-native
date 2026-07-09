#pragma once

#include <cstdint>
#include <filesystem>
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
