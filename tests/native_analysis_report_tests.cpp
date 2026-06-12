#include "core/analysis_core.h"
#include "core/report_core.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

using namespace svm::core::analysis;

RegisterSample sample(int address, std::uint16_t value) {
    RegisterSample result;
    result.observationId = address + 1000;
    result.sessionId = "scan-session-1";
    result.slaveId = 1;
    result.functionCode = 3;
    result.address = address;
    result.value = value;
    result.blockIndex = 2;
    result.attemptIndex = 1;
    return result;
}

void numericDecoderMatchesLegacySemantics() {
    const auto uint16Value = decodeNumericValue(NumericCandidateType::UInt16,
                                                WordOrder::HighWordFirst,
                                                ByteOrder::LittleEndian,
                                                {0x1234});
    assert(uint16Value.has_value());
    assert(*uint16Value == 0x3412);

    const auto float32Value = decodeNumericValue(NumericCandidateType::Float32,
                                                 WordOrder::HighWordFirst,
                                                 ByteOrder::BigEndian,
                                                 {0x4148, 0x0000});
    assert(float32Value.has_value());
    assert(std::abs(*float32Value - 12.5) < 0.000001);

    const auto packedBcd = decodePackedBcdWords({0x5678, 0x1234}, WordOrder::LowWordFirst, ByteOrder::BigEndian);
    assert(packedBcd.has_value());
    assert(*packedBcd == 12345678.0);

    const auto gray = decodeNumericValue("Gray16", "HighWordFirst", "LittleEndian", {0x5600});
    assert(gray.has_value());
    assert(*gray == 100.0);
}

void candidateGenerationProducesChineseEvidence() {
    CandidateGenerationOptions options;
    options.includeInt16 = false;
    options.includeUInt32 = false;
    options.includeInt32 = false;
    options.includeFloat32 = false;
    options.includePackedBCD = false;
    options.includeGray16 = false;
    options.includeBitFlags = false;
    options.scaleTransforms = {{1.0, 0.0}, {0.01, 0.0}};
    options.tolerance.absolute = 0.02;

    TargetValue target;
    target.label = "目标值";
    target.value = 12.35;
    target.unit = "unit";

    const auto result = generateValueCandidates({sample(100, 1234)}, target, options);
    assert(result.success);
    assert(result.candidates.size() == 1);
    const auto& candidate = result.candidates.front();
    assert(candidate.type == NumericCandidateType::UInt16);
    assert(candidate.startAddress == 100);
    assert(candidate.decodedValue == 1234.0);
    assert(candidate.scale.multiplier == 0.01);
    assert(candidate.engineeringValue == 12.34);
    assert(candidate.evidenceText.find("单样本候选") != std::string::npos);
}

CandidateObservation observation(std::string runId,
                                 double targetValue,
                                 double engineeringValue,
                                 double absoluteError,
                                 double candidateScore) {
    CandidateObservation result;
    result.runId = std::move(runId);
    result.sourceScanSessionId = "scan-" + result.runId;
    result.candidateType = "Float32";
    result.wordOrder = "HighWordFirst";
    result.byteOrder = "BigEndian";
    result.sourceSessionId = result.sourceScanSessionId;
    result.slaveId = 1;
    result.functionCode = 3;
    result.startAddress = 100;
    result.registerCount = 2;
    result.targetValue = targetValue;
    result.engineeringValue = engineeringValue;
    result.absoluteError = absoluteError;
    result.effectiveTolerance = 0.10;
    result.candidateScore = candidateScore;
    result.observationIds = {1100, 1101};
    result.addresses = {100, 101};
    return result;
}

void stabilityAnalysisKeepsChineseConfidence() {
    StabilityAnalysisOptions options;
    options.minimumSampleCount = 2;
    options.strongSampleCount = 3;

    const auto result = analyzeCandidateStability({
        observation("run-1", 12.34, 12.34, 0.00, 100.0),
        observation("run-2", 20.00, 20.01, 0.01, 96.0),
        observation("run-3", 33.30, 33.28, 0.02, 94.0),
    }, options);

    assert(result.success);
    assert(result.candidates.size() == 1);
    const auto& candidate = result.candidates.front();
    assert(candidate.sampleCount == 3);
    assert(candidate.meetsMinimumSampleCount);
    assert(candidate.confidenceLevel == "高");
    assert(candidate.stabilityScore > 90.0);
    assert(candidate.evidenceSummary.find("置信等级 高") != std::string::npos);
}

void ruleInterpretationValidatesChineseMaps() {
    const auto validation = validateInterpretationMap(
        "BitFlags",
        "0=运行允许|未允许|已允许\nbit1=报警|正常|报警触发");
    assert(validation.valid);
    assert(validation.definitionCount == 2);
    assert(validation.previewText.find("已识别 2 个位定义") != std::string::npos);
    assert(validation.previewText.find("bit0 运行允许") != std::string::npos);

    const auto bitText = bitFlagInterpretationText(
        "0=运行允许|未允许|已允许\nbit1=报警|正常|报警触发",
        0x0001);
    assert(bitText.find("位解释：bit0 运行允许=已允许") != std::string::npos);
    assert(bitText.find("bit1 报警=正常") != std::string::npos);

    const std::string map = "0=停止\n1=运行\n0x10=维护";
    const auto enumValidation = validateInterpretationMap("EnumMap", map);
    assert(enumValidation.valid);
    assert(enumValidation.definitionCount == 3);
    assert(enumMapInterpretationText(map, 16) == "枚举解释：维护。");
    assert(enumMapInterpretationText(map, 2).find("未定义枚举值 2") != std::string::npos);
}

void markdownReportRendersUtf8Chinese() {
    svm::core::report::RuleVerificationRun run;
    run.verificationRunId = "verify-1";
    run.sourceScanSessionId = "scan-1";
    run.ruleCount = 2;
    run.verifiedCount = 1;
    run.missingCount = 1;
    run.createdAtText = "2026-06-04 10:00:00";

    svm::core::report::RuleVerificationResult verified;
    verified.fieldName = "温度|出水";
    verified.unit = "℃";
    verified.verified = true;
    verified.statusText = "已验证";
    verified.slaveId = 1;
    verified.functionCode = 3;
    verified.startAddress = 100;
    verified.registerCount = 2;
    verified.observationIds = {11, 12};
    verified.rawRegisters = {0x4148, 0x0000};
    verified.engineeringValue = 12.5;
    verified.interpretationText = "位解释：bit0 运行允许=已允许。";
    verified.evidenceText = "字段验证成功\n来自扫描观测";

    svm::core::report::RuleVerificationResult missing;
    missing.fieldName = "压力";
    missing.unit = "kPa";
    missing.verified = false;
    missing.statusText = "缺少地址 200 的观测，无法验证。";
    missing.slaveId = 1;
    missing.functionCode = 3;
    missing.startAddress = 200;
    missing.registerCount = 1;
    missing.evidenceText = missing.statusText;

    const auto markdown = svm::core::report::renderRuleVerificationMarkdownReport(run, {verified, missing});
    assert(markdown.find("# 协议规则验证报告") == 0);
    assert(markdown.find("温度\\|出水") != std::string::npos);
    assert(markdown.find("12.5") != std::string::npos);
    assert(markdown.find("0x4148, 0x0000") != std::string::npos);
    assert(markdown.find("11, 12") != std::string::npos);
    assert(markdown.find("字段验证成功<br>来自扫描观测") != std::string::npos);
    assert(markdown.find("不会修改原始扫描") != std::string::npos);
}

} // namespace

int main() {
    numericDecoderMatchesLegacySemantics();
    candidateGenerationProducesChineseEvidence();
    stabilityAnalysisKeepsChineseConfidence();
    ruleInterpretationValidatesChineseMaps();
    markdownReportRendersUtf8Chinese();

    std::cout << "native_analysis_report_tests passed\n";
    return 0;
}
