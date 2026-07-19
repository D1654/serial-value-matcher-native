#include "report/evidence_bundle_writer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string_view>
#include <system_error>

namespace svm::report {
namespace {

constexpr std::string_view kRedacted = "[redacted]";
constexpr std::array<std::string_view, 6> kBundleFileNames = {
    "summary.md",
    "app_version.txt",
    "raw_events.tsv",
    "scan_settings.txt",
    "report_metadata.txt",
    "rule_verification_report.md",
};

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool containsAny(std::string_view text, const std::initializer_list<std::string_view> needles) {
    const std::string lowered = toLower(std::string(text));
    return std::any_of(needles.begin(), needles.end(), [&lowered](std::string_view needle) {
        return lowered.find(needle) != std::string::npos;
    });
}

bool isWindowsDrivePath(std::string_view value) {
    return value.size() >= 3
        && std::isalpha(static_cast<unsigned char>(value[0])) != 0
        && value[1] == ':'
        && (value[2] == '\\' || value[2] == '/');
}

bool isAbsolutePath(std::string_view value) {
    return value.starts_with('/')
        || value.starts_with("\\\\")
        || isWindowsDrivePath(value);
}

bool isPathStart(std::string_view value) {
    if (value.starts_with("\\\\") || isWindowsDrivePath(value)) {
        return true;
    }
    return value.size() >= 2
        && value[0] == '/'
        && std::isspace(static_cast<unsigned char>(value[1])) == 0;
}

bool shouldRedactPath(std::string_view key, std::string_view value, const EvidenceBundleRedactionOptions& redaction) {
    return redaction.redactAbsolutePaths
        && (containsAny(key, {"path", "file", "directory", "dir", "artifact"})
            || isAbsolutePath(value));
}

bool shouldRedactDevice(std::string_view key, const EvidenceBundleRedactionOptions& redaction) {
    return redaction.redactDeviceIdentifiers
        && containsAny(key, {"device", "serial", "port", "endpoint", "com", "driver", "chip"});
}

std::string redactValue(
    std::string_view key,
    std::string_view value,
    const EvidenceBundleRedactionOptions& redaction) {
    if (shouldRedactPath(key, value, redaction) || shouldRedactDevice(key, redaction)) {
        return std::string(kRedacted);
    }
    return std::string(value);
}

bool isTextBoundary(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0
        || ch == '\t'
        || ch == '|'
        || ch == ','
        || ch == ';'
        || ch == '"'
        || ch == '\''
        || ch == '`'
        || ch == '<'
        || ch == '>'
        || ch == '('
        || ch == ')'
        || ch == '['
        || ch == ']';
}

bool isDeviceTokenChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0
        || ch == '-'
        || ch == '_'
        || ch == '.';
}

bool startsWithCaseInsensitive(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(text[index]))
            != std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

std::size_t consumeTextToken(std::string_view text, std::size_t offset) {
    std::size_t end = offset;
    while (end < text.size() && !isTextBoundary(text[end])) {
        ++end;
    }
    return end;
}

std::size_t consumeComPortToken(std::string_view text, std::size_t offset) {
    if (offset > 0 && !isTextBoundary(text[offset - 1])) {
        return offset;
    }
    const std::string_view suffix = text.substr(offset);
    if (!startsWithCaseInsensitive(suffix, "COM") || suffix.size() < 4) {
        return offset;
    }
    std::size_t end = offset + 3;
    if (std::isdigit(static_cast<unsigned char>(text[end])) == 0) {
        return offset;
    }
    while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
        ++end;
    }
    return end;
}

std::size_t consumeSerialNumberToken(std::string_view text, std::size_t offset) {
    if (offset > 0 && !isTextBoundary(text[offset - 1])) {
        return offset;
    }
    const std::string_view suffix = text.substr(offset);
    if (!startsWithCaseInsensitive(suffix, "SN-") || suffix.size() < 4) {
        return offset;
    }
    std::size_t end = offset + 3;
    while (end < text.size() && isDeviceTokenChar(text[end])) {
        ++end;
    }
    return end;
}

std::string redactFreeText(std::string_view text, const EvidenceBundleRedactionOptions& redaction) {
    if (!redaction.redactAbsolutePaths && !redaction.redactDeviceIdentifiers) {
        return std::string(text);
    }

    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (redaction.redactAbsolutePaths && isPathStart(text.substr(index))) {
            output += kRedacted;
            index = consumeTextToken(text, index);
            continue;
        }

        if (redaction.redactDeviceIdentifiers) {
            std::size_t end = consumeComPortToken(text, index);
            if (end != index) {
                output += kRedacted;
                index = end;
                continue;
            }

            end = consumeSerialNumberToken(text, index);
            if (end != index) {
                output += kRedacted;
                index = end;
                continue;
            }
        }

        output.push_back(text[index]);
        ++index;
    }
    return output;
}

std::string renderRuleVerificationReportFile(
    std::string_view markdown,
    const EvidenceBundleRedactionOptions& redaction) {
    if (redaction.redactRawPayload) {
        return "# 协议规则验证报告\n\n"
            "报告正文已因原始业务载荷脱敏被移除。需要查看完整报告时，请在可信本机环境重新导出完整证据包。\n";
    }
    return redactFreeText(markdown, redaction);
}

std::string escapeTsv(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\t':
            output += "\\t";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\n':
            output += "\\n";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

std::string hexPayload(const std::vector<std::uint8_t>& payload) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < payload.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << std::setw(2) << static_cast<unsigned int>(payload[index]);
    }
    return output.str();
}

std::string payloadText(const std::vector<std::uint8_t>& payload, const EvidenceBundleRedactionOptions& redaction) {
    if (redaction.redactRawPayload) {
        return "[redacted " + std::to_string(payload.size()) + " bytes]";
    }
    return hexPayload(payload);
}

std::string boolText(bool value) {
    return value ? "yes" : "no";
}

std::string fileHeader(std::string_view title) {
    return "# " + std::string(title) + "\n\n";
}

bool writeTextFile(const std::filesystem::path& path, std::string_view text, std::string* errorMessage) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        *errorMessage = "无法打开文件：" + path.string();
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        *errorMessage = "无法完整写入文件：" + path.string();
        return false;
    }
    return true;
}

bool writeBundleFile(
    const std::filesystem::path& directory,
    std::string_view fileName,
    std::string_view text,
    EvidenceBundleWriteResult* result) {
    const std::filesystem::path path = directory / fileName;
    if (!writeTextFile(path, text, &result->errorMessage)) {
        return false;
    }
    result->writtenFiles.push_back(path);
    return true;
}

bool removeStaleBundleFiles(const std::filesystem::path& directory, EvidenceBundleWriteResult* result) {
    for (std::string_view fileName : kBundleFileNames) {
        std::error_code error;
        const std::filesystem::path path = directory / fileName;
        std::filesystem::remove(path, error);
        if (error) {
            result->errorMessage = "无法清理旧证据包文件：" + path.string() + "；" + error.message();
            return false;
        }
    }
    return true;
}

} // namespace

std::string renderEvidenceBundleSummary(
    const EvidenceBundleInput& input,
    const EvidenceBundleRedactionOptions& redaction,
    bool includeRawEvents) {
    std::ostringstream output;
    output << fileHeader(input.title.empty() ? "Evidence bundle summary" : input.title);
    output << "Local-only export: yes\n";
    output << "Upload behavior: none\n";
    output << "Generated UTC: " << (input.generatedAtUtc.empty() ? "not provided" : input.generatedAtUtc) << '\n';
    output << "Raw event count: " << input.rawEvents.size() << '\n';
    output << "Raw events included: " << boolText(includeRawEvents) << '\n';
    output << "App version fields: " << input.appVersion.size() << '\n';
    output << "Scan setting fields: " << input.scanSettings.size() << '\n';
    output << "Report metadata fields: " << input.reportMetadata.size() << '\n';
    output << "Rule verification report: " << boolText(!input.ruleVerificationReportMarkdown.empty()) << '\n';
    output << "Redact absolute paths: " << boolText(redaction.redactAbsolutePaths) << '\n';
    output << "Redact device identifiers: " << boolText(redaction.redactDeviceIdentifiers) << '\n';
    output << "Redact raw payload: " << boolText(redaction.redactRawPayload) << '\n';
    output << "\nFiles:\n";
    output << "- summary.md\n";
    output << "- app_version.txt\n";
    if (includeRawEvents) {
        output << "- raw_events.tsv\n";
    }
    if (!input.scanSettings.empty()) {
        output << "- scan_settings.txt\n";
    }
    if (!input.reportMetadata.empty()) {
        output << "- report_metadata.txt\n";
    }
    if (!input.ruleVerificationReportMarkdown.empty()) {
        output << "- rule_verification_report.md\n";
    }
    output << "\nSharing note: review exported files before sending them outside the local machine.\n";
    return output.str();
}

std::string renderEvidenceBundleKeyValues(
    const std::vector<EvidenceBundleKeyValue>& values,
    const EvidenceBundleRedactionOptions& redaction) {
    std::ostringstream output;
    output << "key\tvalue\n";
    for (const EvidenceBundleKeyValue& value : values) {
        output << escapeTsv(value.key) << '\t'
               << escapeTsv(redactValue(value.key, value.value, redaction)) << '\n';
    }
    return output.str();
}

std::string renderEvidenceBundleRawEvents(
    const std::vector<EvidenceBundleRawEvent>& events,
    const EvidenceBundleRedactionOptions& redaction) {
    std::ostringstream output;
    output << "id\tsession_id\tdirection\ttimestamp_utc\tendpoint\tpayload_hex"
              "\toperation\trequest_id\tgeneration\tstatus\tdeadline_status\tbyte_count"
              "\terror_category\tnative_code\tcomm_error_mask\tinput_queue_bytes\toutput_queue_bytes\n";
    for (const EvidenceBundleRawEvent& event : events) {
        output << event.id << '\t'
               << escapeTsv(event.sessionId) << '\t'
               << escapeTsv(event.direction) << '\t'
               << escapeTsv(event.timestampUtc) << '\t'
               << escapeTsv(redactValue("endpoint", event.endpoint, redaction)) << '\t'
               << escapeTsv(payloadText(event.payload, redaction)) << '\t'
               << escapeTsv(event.operation) << '\t'
               << event.requestId << '\t'
               << event.generation << '\t'
               << escapeTsv(event.status) << '\t'
               << escapeTsv(event.deadlineStatus) << '\t'
               << event.byteCount << '\t'
               << escapeTsv(event.errorCategory) << '\t'
               << event.nativeCode << '\t'
               << event.commErrorMask << '\t';
        if (event.inputQueueBytes.has_value()) {
            output << *event.inputQueueBytes;
        }
        output << '\t';
        if (event.outputQueueBytes.has_value()) {
            output << *event.outputQueueBytes;
        }
        output << '\n';
    }
    return output.str();
}

EvidenceBundleWriteResult writeEvidenceBundle(
    const EvidenceBundleInput& input,
    const EvidenceBundleWriteOptions& options) {
    EvidenceBundleWriteResult result;
    if (options.outputDirectory.empty()) {
        result.errorMessage = "证据包输出目录不能为空。";
        return result;
    }

    std::error_code error;
    std::filesystem::create_directories(options.outputDirectory, error);
    if (error) {
        result.errorMessage = "无法创建证据包目录：" + options.outputDirectory.string() + "；" + error.message();
        return result;
    }
    if (!removeStaleBundleFiles(options.outputDirectory, &result)) {
        return result;
    }

    if (!writeBundleFile(options.outputDirectory, "summary.md", renderEvidenceBundleSummary(input, options.redaction, options.includeRawEvents), &result)) {
        return result;
    }
    if (!writeBundleFile(options.outputDirectory, "app_version.txt", renderEvidenceBundleKeyValues(input.appVersion, options.redaction), &result)) {
        return result;
    }
    if (options.includeRawEvents
        && !writeBundleFile(options.outputDirectory, "raw_events.tsv", renderEvidenceBundleRawEvents(input.rawEvents, options.redaction), &result)) {
        return result;
    }
    if (!input.scanSettings.empty()
        && !writeBundleFile(options.outputDirectory, "scan_settings.txt", renderEvidenceBundleKeyValues(input.scanSettings, options.redaction), &result)) {
        return result;
    }
    if (!input.reportMetadata.empty()
        && !writeBundleFile(options.outputDirectory, "report_metadata.txt", renderEvidenceBundleKeyValues(input.reportMetadata, options.redaction), &result)) {
        return result;
    }
    if (!input.ruleVerificationReportMarkdown.empty()
        && !writeBundleFile(
            options.outputDirectory,
            "rule_verification_report.md",
            renderRuleVerificationReportFile(input.ruleVerificationReportMarkdown, options.redaction),
            &result)) {
        return result;
    }

    result.success = true;
    return result;
}

} // namespace svm::report
