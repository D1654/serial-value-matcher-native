#include "core/report_core.h"

#include <iomanip>
#include <sstream>

namespace svm::core::report {
namespace {

Text replaceAll(Text value, const Text& from, const Text& to) {
    if (from.empty()) {
        return value;
    }
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != Text::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

Text escapeMarkdownCell(Text text) {
    text = replaceAll(std::move(text), "|", "\\|");
    text = replaceAll(std::move(text), "\n", "<br>");
    text = replaceAll(std::move(text), "\r", "");
    return text;
}

Text formatTime(const Text& value) {
    return value.empty() ? "未知" : value;
}

Text formatDouble(double value) {
    std::ostringstream stream;
    stream << std::setprecision(12) << value;
    return stream.str();
}

Text formatRegisters(const std::vector<int>& registers) {
    if (registers.empty()) {
        return "-";
    }

    Text output;
    for (std::size_t index = 0; index < registers.size(); ++index) {
        if (index != 0) {
            output += ", ";
        }
        std::ostringstream stream;
        stream << "0x" << std::hex << std::nouppercase << std::setw(4) << std::setfill('0') << (registers[index] & 0xFFFF);
        output += stream.str();
    }
    return output;
}

Text formatObservationIds(const std::vector<std::int64_t>& ids) {
    if (ids.empty()) {
        return "-";
    }

    Text output;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index != 0) {
            output += ", ";
        }
        output += std::to_string(ids[index]);
    }
    return output;
}

void appendLine(Text& output, const Text& line = {}) {
    output += line;
    output += '\n';
}

} // namespace

Text renderRuleVerificationMarkdownReport(
    const RuleVerificationRun& run,
    const std::vector<RuleVerificationResult>& results) {
    Text markdown;
    appendLine(markdown, "# 协议规则验证报告");
    appendLine(markdown);
    appendLine(markdown, "## 验证摘要");
    appendLine(markdown);
    appendLine(markdown, "- 验证运行 ID：" + run.verificationRunId);
    appendLine(markdown, "- 来源扫描会话：" + run.sourceScanSessionId);
    appendLine(markdown, "- 创建时间：" + formatTime(run.createdAtText));
    appendLine(markdown, "- 总规则数：" + std::to_string(run.ruleCount));
    appendLine(markdown, "- 已验证：" + std::to_string(run.verifiedCount));
    appendLine(markdown, "- 缺少观测：" + std::to_string(run.missingCount));
    appendLine(markdown, "- 暂不支持/失败：" + std::to_string(run.unsupportedCount));
    appendLine(markdown);
    appendLine(markdown, "## 规则验证明细");
    appendLine(markdown);

    if (results.empty()) {
        appendLine(markdown, "暂无验证明细。");
        appendLine(markdown);
        return markdown;
    }

    appendLine(markdown, "| 结果 | 字段 | 工程值 | 单位 | 从站/功能码 | 地址 | 原始寄存器 | Observation IDs | 解释 | 证据 |");
    appendLine(markdown, "| --- | --- | ---: | --- | --- | --- | --- | --- | --- | --- |");
    for (const RuleVerificationResult& result : results) {
        const Text status = result.verified ? "已验证" : result.statusText;
        const Text engineeringValue = result.verified ? formatDouble(result.engineeringValue) : "-";
        const Text slaveFunction = "从站 " + std::to_string(result.slaveId) + " / FC" + std::to_string(result.functionCode);
        const Text address = std::to_string(result.startAddress) + "（" + std::to_string(result.registerCount) + " 个寄存器）";
        appendLine(markdown, "| " + escapeMarkdownCell(status)
            + " | " + escapeMarkdownCell(result.fieldName)
            + " | " + escapeMarkdownCell(engineeringValue)
            + " | " + escapeMarkdownCell(result.unit)
            + " | " + escapeMarkdownCell(slaveFunction)
            + " | " + escapeMarkdownCell(address)
            + " | " + escapeMarkdownCell(formatRegisters(result.rawRegisters))
            + " | " + escapeMarkdownCell(formatObservationIds(result.observationIds))
            + " | " + escapeMarkdownCell(result.interpretationText.empty() ? "-" : result.interpretationText)
            + " | " + escapeMarkdownCell(result.evidenceText) + " |");
    }
    appendLine(markdown);
    appendLine(markdown, "## 说明");
    appendLine(markdown);
    appendLine(markdown, "- “缺少观测”表示所选扫描会话没有覆盖规则所需的地址，不等同于设备异常。");
    appendLine(markdown, "- “暂不支持/失败”表示当前规则类型或解码结果暂不能生成有效工程值，后续可通过扩展规则类型补齐。");
    appendLine(markdown, "- 本报告基于已确认规则和扫描观测生成，不会修改原始扫描、候选或规则。");
    appendLine(markdown);
    return markdown;
}

} // namespace svm::core::report
