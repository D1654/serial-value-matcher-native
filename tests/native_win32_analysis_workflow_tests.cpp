#include "win32/native_analysis_workflow.h"

#include <cassert>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace storage = svm::native_storage;
namespace win32 = svm::win32;

storage::ScanSessionRecord scanSession() {
    storage::ScanSessionRecord session;
    session.sessionId = "scan-1";
    session.slaveId = 1;
    session.functionCode = 3;
    session.startAddress = 100;
    session.endAddress = 101;
    session.blockSize = 2;
    session.status = "completed";
    return session;
}

storage::ScanObservationRecord observation(std::int64_t id,
                                           int address,
                                           int value,
                                           std::string observedAtUtc = "2026-06-22T00:00:00Z") {
    storage::ScanObservationRecord record;
    record.id = id;
    record.sessionId = "scan-1";
    record.blockIndex = 0;
    record.attemptIndex = 0;
    record.slaveId = 1;
    record.functionCode = 3;
    record.address = address;
    record.value = value;
    record.observedAtUtc = std::move(observedAtUtc);
    return record;
}

storage::ProtocolFieldRuleRecord rule(std::string ruleId,
                                      std::string fieldName,
                                      std::string candidateType,
                                      int startAddress,
                                      int registerCount) {
    storage::ProtocolFieldRuleRecord record;
    record.ruleId = std::move(ruleId);
    record.fieldName = std::move(fieldName);
    record.candidateType = std::move(candidateType);
    record.wordOrder = "HighWordFirst";
    record.byteOrder = "BigEndian";
    record.slaveId = 1;
    record.functionCode = 3;
    record.startAddress = startAddress;
    record.registerCount = registerCount;
    record.scaleMultiplier = 0.01;
    record.scaleOffset = 0.0;
    record.unit = "C";
    return record;
}

void candidateAnalysisBuildsRunAndCandidates() {
    const std::vector<storage::ScanObservationRecord> observations = {
        observation(11, 100, 1234, "observed-1"),
    };

    const win32::NativeCandidateAnalysisBuildResult build = win32::nativeBuildCandidateAnalysisRun(
        scanSession(),
        observations,
        "temperature",
        12.34,
        "C",
        0.01,
        "match-1",
        "sampled-at",
        "created-at",
        "observed-at");

    assert(build.success);
    assert(build.errorMessage.empty());
    assert(build.run.runId == "match-1");
    assert(build.run.sourceScanSessionId == "scan-1");
    assert(build.run.targetLabel == "temperature");
    assert(std::abs(build.run.targetValue - 12.34) < 0.000001);
    assert(build.run.targetUnit == "C");
    assert(build.run.sampledAtUtc == "sampled-at");
    assert(build.run.createdAtUtc == "created-at");
    assert(!build.candidates.empty());

    bool foundScaledUInt16 = false;
    for (const storage::MatchCandidateRecord& candidate : build.candidates) {
        if (candidate.candidateType == "UInt16"
            && candidate.sourceSessionId == "scan-1"
            && candidate.startAddress == 100
            && candidate.rawRegisters == std::vector<int>{1234}
            && std::abs(candidate.engineeringValue - 12.34) < 0.000001
            && candidate.observedAtUtc == "observed-at") {
            foundScaledUInt16 = true;
            break;
        }
    }
    assert(foundScaledUInt16);
}

void ruleVerificationClassifiesVerifiedMissingAndUnsupported() {
    const std::vector<storage::ScanObservationRecord> observations = {
        observation(21, 100, 0x1234, "first-100"),
        observation(22, 100, 0x5678, "latest-100"),
    };
    const std::vector<storage::ProtocolFieldRuleRecord> rules = {
        rule("rule-ok", "temperature", "UInt16", 100, 1),
        rule("rule-missing", "pressure", "UInt16", 200, 1),
        rule("rule-unsupported", "float-value", "Float32", 100, 1),
    };

    const win32::NativeRuleVerificationBuildResult build = win32::nativeBuildRuleVerificationResult(
        scanSession(),
        rules,
        observations,
        "verify-1",
        "created-at");

    assert(build.run.verificationRunId == "verify-1");
    assert(build.run.sourceScanSessionId == "scan-1");
    assert(build.run.ruleCount == 3);
    assert(build.run.verifiedCount == 1);
    assert(build.run.missingCount == 1);
    assert(build.run.unsupportedCount == 1);
    assert(build.results.size() == 3);

    const storage::RuleVerificationResultRecord& verified = build.results[0];
    assert(verified.verified);
    assert(verified.ruleId == "rule-ok");
    assert(verified.observationIds == std::vector<std::int64_t>{22});
    assert(verified.rawRegisters == std::vector<int>{0x5678});
    assert(std::abs(verified.engineeringValue - 221.36) < 0.000001);
    assert(verified.observedAtUtc == "latest-100");
    assert(verified.statusText == "已验证");

    const storage::RuleVerificationResultRecord& missing = build.results[1];
    assert(!missing.verified);
    assert(missing.ruleId == "rule-missing");
    assert(missing.statusText.find("200") != std::string::npos);

    const storage::RuleVerificationResultRecord& unsupported = build.results[2];
    assert(!unsupported.verified);
    assert(unsupported.ruleId == "rule-unsupported");
    assert(unsupported.statusText.find("解码失败") != std::string::npos);
}

void ruleVerificationDecodesMultiRegisterAndInterpretationMaps() {
    const std::vector<storage::ScanObservationRecord> observations = {
        observation(31, 100, 0x4148, "float-high"),
        observation(32, 101, 0x0000, "float-low"),
        observation(33, 102, 0x0001, "bits"),
        observation(34, 103, 0x0010, "enum"),
    };

    storage::ProtocolFieldRuleRecord floatRule = rule("rule-float", "temperature", "Float32", 100, 2);
    floatRule.scaleMultiplier = 1.0;

    storage::ProtocolFieldRuleRecord bitRule = rule("rule-bits", "status", "BitFlags", 102, 1);
    bitRule.scaleMultiplier = 1.0;
    bitRule.interpretationMap = "0=运行允许|未允许|已允许\nbit1=报警|正常|报警触发";

    storage::ProtocolFieldRuleRecord enumRule = rule("rule-enum", "mode", "EnumMap", 103, 1);
    enumRule.scaleMultiplier = 1.0;
    enumRule.interpretationMap = "0=停止\n1=运行\n0x10=维护";

    const win32::NativeRuleVerificationBuildResult build = win32::nativeBuildRuleVerificationResult(
        scanSession(),
        {floatRule, bitRule, enumRule},
        observations,
        "verify-typed",
        "created-at");

    assert(build.run.ruleCount == 3);
    assert(build.run.verifiedCount == 3);
    assert(build.run.missingCount == 0);
    assert(build.run.unsupportedCount == 0);
    assert(build.results.size() == 3);

    const storage::RuleVerificationResultRecord& floatResult = build.results[0];
    assert(floatResult.verified);
    assert(floatResult.observationIds == std::vector<std::int64_t>({31, 32}));
    assert(floatResult.rawRegisters == std::vector<int>({0x4148, 0x0000}));
    assert(std::abs(floatResult.engineeringValue - 12.5) < 0.000001);
    assert(floatResult.observedAtUtc == "float-low");

    const storage::RuleVerificationResultRecord& bitResult = build.results[1];
    assert(bitResult.verified);
    assert(bitResult.interpretationText.find("bit0 运行允许=已允许") != std::string::npos);
    assert(bitResult.interpretationText.find("bit1 报警=正常") != std::string::npos);

    const storage::RuleVerificationResultRecord& enumResult = build.results[2];
    assert(enumResult.verified);
    assert(enumResult.interpretationText == "枚举解释：维护。");
}

void markdownReportRendersFromNativeRecords() {
    storage::RuleVerificationRunRecord run;
    run.verificationRunId = "verify-1";
    run.sourceScanSessionId = "scan-1";
    run.ruleCount = 1;
    run.verifiedCount = 1;
    run.createdAtUtc = "created-at";

    storage::RuleVerificationResultRecord result;
    result.verificationRunId = "verify-1";
    result.ruleId = "rule-ok";
    result.fieldName = "temperature";
    result.unit = "C";
    result.candidateType = "UInt16";
    result.verified = true;
    result.statusText = "已验证";
    result.slaveId = 1;
    result.functionCode = 3;
    result.startAddress = 100;
    result.registerCount = 1;
    result.observationIds = {22};
    result.rawRegisters = {0x5678};
    result.engineeringValue = 221.36;
    result.evidenceText = "字段验证成功：temperature。";

    const std::string markdown = win32::nativeRenderRuleVerificationMarkdownReport(run, {result});
    assert(markdown.find("# 协议规则验证报告") == 0);
    assert(markdown.find("verify-1") != std::string::npos);
    assert(markdown.find("temperature") != std::string::npos);
    assert(markdown.find("0x5678") != std::string::npos);
    assert(markdown.find("不会修改原始扫描") != std::string::npos);
}

} // namespace

int main() {
    candidateAnalysisBuildsRunAndCandidates();
    ruleVerificationClassifiesVerifiedMissingAndUnsupported();
    ruleVerificationDecodesMultiRegisterAndInterpretationMaps();
    markdownReportRendersFromNativeRecords();

    std::cout << "native_win32_analysis_workflow_tests passed\n";
    return 0;
}
