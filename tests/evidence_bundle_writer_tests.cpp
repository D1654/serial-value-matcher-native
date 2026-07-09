#include "report/evidence_bundle_writer.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

template <typename Function>
void runEvidenceBundleTest(const char* name, Function function) {
    std::cerr << "evidence_bundle_writer_tests: " << name << std::endl;
    function();
}

std::filesystem::path temporaryBundlePath() {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("svm-evidence-bundle-test-" + std::to_string(suffix));
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool pathExists(const std::filesystem::path& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    assert(!error);
    return exists;
}

svm::report::EvidenceBundleInput makeInput() {
    svm::report::EvidenceBundleInput input;
    input.title = "现场证据包";
    input.generatedAtUtc = "2026-07-09T06:30:00Z";
    input.appVersion = {
        {"app_version", "1.0.4"},
        {"app_path", "/home/user/customer-a/svm-native-win32.exe"},
    };
    input.scanSettings = {
        {"slave_id", "1"},
        {"function_code", "3"},
        {"start_address", "100"},
    };
    input.reportMetadata = {
        {"device_serial", "SN-PRIVATE-001"},
        {"report_path", "C:\\Users\\Alice\\Desktop\\现场报告.md"},
    };
    input.rawEvents = {
        {1, "session-1", "Tx", "2026-07-09T06:30:01Z", "COM12", {0x01, 0x03, 0x00, 0x64}},
        {2, "session-1", "Rx", "2026-07-09T06:30:02Z", "COM12", {0x01, 0x03, 0x02, 0x12, 0x34}},
    };
    input.ruleVerificationReportMarkdown =
        "# 协议规则验证报告\n\n"
        "字段验证成功，设备 COM12，序列号 SN-PRIVATE-001，路径 /home/user/customer-a/report.md，原始载荷 01 03 00 64。\n";
    return input;
}

void fullBundleWritesAllExpectedFiles() {
    const auto directory = temporaryBundlePath();
    const auto result = svm::report::writeEvidenceBundle(makeInput(), {directory, {}, true});
    assert(result.success);
    assert(result.errorMessage.empty());
    assert(result.writtenFiles.size() == 6);

    assert(pathExists(directory / "summary.md"));
    assert(pathExists(directory / "app_version.txt"));
    assert(pathExists(directory / "scan_settings.txt"));
    assert(pathExists(directory / "report_metadata.txt"));
    assert(pathExists(directory / "raw_events.tsv"));
    assert(pathExists(directory / "rule_verification_report.md"));

    const std::string summary = readText(directory / "summary.md");
    assert(summary.find("Local-only export: yes") != std::string::npos);
    assert(summary.find("Upload behavior: none") != std::string::npos);
    assert(summary.find("Raw event count: 2") != std::string::npos);
    assert(summary.find("Raw events included: yes") != std::string::npos);

    const std::string rawEvents = readText(directory / "raw_events.tsv");
    assert(rawEvents.find("COM12") != std::string::npos);
    assert(rawEvents.find("01 03 00 64") != std::string::npos);
    assert(rawEvents.find("01 03 02 12 34") != std::string::npos);

    const std::string appVersion = readText(directory / "app_version.txt");
    assert(appVersion.find("/home/user/customer-a/svm-native-win32.exe") != std::string::npos);

    const std::string report = readText(directory / "rule_verification_report.md");
    assert(report.find("COM12") != std::string::npos);
    assert(report.find("SN-PRIVATE-001") != std::string::npos);
    assert(report.find("01 03 00 64") != std::string::npos);

    std::filesystem::remove_all(directory);
}

void redactedBundleRemovesConfiguredSensitiveFields() {
    const auto directory = temporaryBundlePath();
    svm::report::EvidenceBundleRedactionOptions redaction;
    redaction.redactAbsolutePaths = true;
    redaction.redactDeviceIdentifiers = true;
    redaction.redactRawPayload = true;

    const auto result = svm::report::writeEvidenceBundle(makeInput(), {directory, redaction, true});
    assert(result.success);

    const std::string appVersion = readText(directory / "app_version.txt");
    assert(appVersion.find("/home/user") == std::string::npos);
    assert(appVersion.find("[redacted]") != std::string::npos);

    const std::string metadata = readText(directory / "report_metadata.txt");
    assert(metadata.find("SN-PRIVATE-001") == std::string::npos);
    assert(metadata.find("C:\\Users\\Alice") == std::string::npos);

    const std::string rawEvents = readText(directory / "raw_events.tsv");
    assert(rawEvents.find("COM12") == std::string::npos);
    assert(rawEvents.find("01 03 00 64") == std::string::npos);
    assert(rawEvents.find("[redacted 4 bytes]") != std::string::npos);
    assert(rawEvents.find("[redacted 5 bytes]") != std::string::npos);

    const std::string report = readText(directory / "rule_verification_report.md");
    assert(report.find("COM12") == std::string::npos);
    assert(report.find("SN-PRIVATE-001") == std::string::npos);
    assert(report.find("/home/user") == std::string::npos);
    assert(report.find("01 03 00 64") == std::string::npos);
    assert(report.find("报告正文已因原始业务载荷脱敏被移除") != std::string::npos);

    std::filesystem::remove_all(directory);
}

void reportRedactionCoversPathsAndDeviceTokensWithoutDroppingReport() {
    const auto directory = temporaryBundlePath();
    svm::report::EvidenceBundleRedactionOptions redaction;
    redaction.redactAbsolutePaths = true;
    redaction.redactDeviceIdentifiers = true;

    const auto result = svm::report::writeEvidenceBundle(makeInput(), {directory, redaction, true});
    assert(result.success);

    const std::string report = readText(directory / "rule_verification_report.md");
    assert(report.find("字段验证成功") != std::string::npos);
    assert(report.find("COM12") == std::string::npos);
    assert(report.find("SN-PRIVATE-001") == std::string::npos);
    assert(report.find("/home/user/customer-a") == std::string::npos);
    assert(report.find("01 03 00 64") != std::string::npos);
    assert(report.find("[redacted]") != std::string::npos);

    std::filesystem::remove_all(directory);
}

void missingOptionalFieldsStillWritesRequiredFiles() {
    auto input = makeInput();
    input.scanSettings.clear();
    input.reportMetadata.clear();
    input.ruleVerificationReportMarkdown.clear();
    input.rawEvents.clear();

    const auto directory = temporaryBundlePath();
    const auto result = svm::report::writeEvidenceBundle(input, {directory, {}, true});
    assert(result.success);
    assert(pathExists(directory / "summary.md"));
    assert(pathExists(directory / "app_version.txt"));
    assert(pathExists(directory / "raw_events.tsv"));
    assert(!pathExists(directory / "scan_settings.txt"));
    assert(!pathExists(directory / "report_metadata.txt"));
    assert(!pathExists(directory / "rule_verification_report.md"));

    const std::string rawEvents = readText(directory / "raw_events.tsv");
    assert(rawEvents == "id\tsession_id\tdirection\ttimestamp_utc\tendpoint\tpayload_hex\n");

    std::filesystem::remove_all(directory);
}

void rawEventsCanBeExcludedAndSummaryMatchesFiles() {
    const auto directory = temporaryBundlePath();
    const auto result = svm::report::writeEvidenceBundle(makeInput(), {directory, {}, false});
    assert(result.success);
    assert(pathExists(directory / "summary.md"));
    assert(!pathExists(directory / "raw_events.tsv"));

    const std::string summary = readText(directory / "summary.md");
    assert(summary.find("Raw event count: 2") != std::string::npos);
    assert(summary.find("Raw events included: no") != std::string::npos);
    assert(summary.find("- raw_events.tsv") == std::string::npos);

    std::filesystem::remove_all(directory);
}

void reusedDirectoryDoesNotKeepStaleFilesFromPreviousBundle() {
    const auto directory = temporaryBundlePath();
    auto fullInput = makeInput();
    const auto fullResult = svm::report::writeEvidenceBundle(fullInput, {directory, {}, true});
    assert(fullResult.success);
    assert(pathExists(directory / "raw_events.tsv"));
    assert(pathExists(directory / "scan_settings.txt"));
    assert(pathExists(directory / "report_metadata.txt"));
    assert(pathExists(directory / "rule_verification_report.md"));

    auto minimalInput = makeInput();
    minimalInput.scanSettings.clear();
    minimalInput.reportMetadata.clear();
    minimalInput.ruleVerificationReportMarkdown.clear();

    const auto redactedResult = svm::report::writeEvidenceBundle(
        minimalInput,
        {directory, {true, true, true}, false});
    assert(redactedResult.success);
    assert(pathExists(directory / "summary.md"));
    assert(pathExists(directory / "app_version.txt"));
    assert(!pathExists(directory / "raw_events.tsv"));
    assert(!pathExists(directory / "scan_settings.txt"));
    assert(!pathExists(directory / "report_metadata.txt"));
    assert(!pathExists(directory / "rule_verification_report.md"));

    const std::string summary = readText(directory / "summary.md");
    assert(summary.find("Raw events included: no") != std::string::npos);

    std::filesystem::remove_all(directory);
}

void pathPrivacyCoversUnixWindowsAndUncPaths() {
    svm::report::EvidenceBundleRedactionOptions redaction;
    redaction.redactAbsolutePaths = true;

    const std::vector<svm::report::EvidenceBundleKeyValue> values{
        {"linux_path", "/var/tmp/customer/run.log"},
        {"windows_path", "D:\\customer\\run.log"},
        {"unc_path", "\\\\server\\share\\run.log"},
        {"note", "safe relative text"},
    };

    const std::string text = svm::report::renderEvidenceBundleKeyValues(values, redaction);
    assert(text.find("/var/tmp") == std::string::npos);
    assert(text.find("D:\\customer") == std::string::npos);
    assert(text.find("\\\\server") == std::string::npos);
    assert(text.find("safe relative text") != std::string::npos);
}

void emptyOutputDirectoryFailsClearly() {
    const auto result = svm::report::writeEvidenceBundle(makeInput(), {});
    assert(!result.success);
    assert(result.errorMessage.find("不能为空") != std::string::npos);
}

} // namespace

int main() {
    runEvidenceBundleTest("fullBundleWritesAllExpectedFiles", fullBundleWritesAllExpectedFiles);
    runEvidenceBundleTest("redactedBundleRemovesConfiguredSensitiveFields", redactedBundleRemovesConfiguredSensitiveFields);
    runEvidenceBundleTest("reportRedactionCoversPathsAndDeviceTokensWithoutDroppingReport", reportRedactionCoversPathsAndDeviceTokensWithoutDroppingReport);
    runEvidenceBundleTest("missingOptionalFieldsStillWritesRequiredFiles", missingOptionalFieldsStillWritesRequiredFiles);
    runEvidenceBundleTest("rawEventsCanBeExcludedAndSummaryMatchesFiles", rawEventsCanBeExcludedAndSummaryMatchesFiles);
    runEvidenceBundleTest("reusedDirectoryDoesNotKeepStaleFilesFromPreviousBundle", reusedDirectoryDoesNotKeepStaleFilesFromPreviousBundle);
    runEvidenceBundleTest("pathPrivacyCoversUnixWindowsAndUncPaths", pathPrivacyCoversUnixWindowsAndUncPaths);
    runEvidenceBundleTest("emptyOutputDirectoryFailsClearly", emptyOutputDirectoryFailsClearly);

    std::cout << "evidence_bundle_writer_tests passed\n";
    return 0;
}
