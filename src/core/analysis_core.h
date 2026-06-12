#pragma once

#include "core/text.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace svm::core::analysis {

enum class NumericCandidateType {
    UInt16,
    Int16,
    UInt32,
    Int32,
    Float32,
    PackedBCD,
    Gray16,
    BitFlags,
};

enum class WordOrder {
    HighWordFirst,
    LowWordFirst,
};

enum class ByteOrder {
    BigEndian,
    LittleEndian,
};

struct MatchTolerance {
    double absolute = 0.0;
    double relativeRatio = 0.0;
};

struct ScaleTransform {
    double multiplier = 1.0;
    double offset = 0.0;
};

struct TargetValue {
    Text label;
    double value = 0.0;
    Text unit;
};

struct RegisterSample {
    std::int64_t observationId = 0;
    Text sessionId;
    int slaveId = 0;
    int functionCode = 0;
    int address = 0;
    std::uint16_t value = 0;
    int blockIndex = -1;
    int attemptIndex = 0;
};

struct CandidateGenerationOptions {
    bool includeUInt16 = true;
    bool includeInt16 = true;
    bool includeUInt32 = true;
    bool includeInt32 = true;
    bool includeFloat32 = true;
    bool includePackedBCD = true;
    bool includeGray16 = true;
    bool includeBitFlags = true;
    std::vector<ScaleTransform> scaleTransforms = {{1.0, 0.0}};
    MatchTolerance tolerance;
    int maxCandidates = 200;
};

struct ValueMatchCandidate {
    NumericCandidateType type = NumericCandidateType::UInt16;
    WordOrder wordOrder = WordOrder::HighWordFirst;
    ByteOrder byteOrder = ByteOrder::BigEndian;
    Text sessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    std::vector<std::int64_t> observationIds;
    std::vector<int> addresses;
    std::vector<int> blockIndexes;
    std::vector<int> attemptIndexes;
    std::vector<std::uint16_t> rawRegisters;
    double decodedValue = 0.0;
    ScaleTransform scale;
    double engineeringValue = 0.0;
    double delta = 0.0;
    double absoluteError = 0.0;
    double effectiveTolerance = 0.0;
    double score = 0.0;
    Text evidenceText;
};

struct CandidateGenerationResult {
    bool success = false;
    Text errorMessage;
    std::vector<ValueMatchCandidate> candidates;
};

struct CandidateObservation {
    Text runId;
    Text sourceScanSessionId;
    Text candidateType;
    Text wordOrder;
    Text byteOrder;
    Text sourceSessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    double targetValue = 0.0;
    double engineeringValue = 0.0;
    double absoluteError = 0.0;
    double effectiveTolerance = 0.0;
    double candidateScore = 0.0;
    std::vector<std::int64_t> observationIds;
    std::vector<int> addresses;
    std::vector<int> blockIndexes;
    std::vector<int> attemptIndexes;
};

struct StabilityAnalysisOptions {
    int minimumSampleCount = 2;
    int strongSampleCount = 4;
};

struct StableCandidate {
    Text candidateType;
    Text wordOrder;
    Text byteOrder;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int registerCount = 0;
    double scaleMultiplier = 1.0;
    double scaleOffset = 0.0;
    int sampleCount = 0;
    bool meetsMinimumSampleCount = false;
    Text confidenceLevel;
    std::vector<Text> runIds;
    std::vector<Text> sourceScanSessionIds;
    std::vector<std::int64_t> observationIds;
    std::vector<int> addresses;
    double meanTargetValue = 0.0;
    double meanEngineeringValue = 0.0;
    double meanAbsoluteError = 0.0;
    double maxAbsoluteError = 0.0;
    double meanCandidateScore = 0.0;
    double meanErrorQuality = 0.0;
    double sampleQuality = 0.0;
    double stabilityScore = 0.0;
    Text evidenceSummary;
};

struct StabilityAnalysisResult {
    bool success = false;
    Text errorMessage;
    std::vector<StableCandidate> candidates;
};

struct BitFlagInterpretationDefinition {
    int bitIndex = -1;
    Text name;
    Text inactiveText;
    Text activeText;
};

struct EnumMapInterpretationDefinition {
    int value = 0;
    Text label;
};

struct InterpretationMapValidationResult {
    bool valid = true;
    int definitionCount = 0;
    std::vector<Text> errors;
    Text previewText;
};

std::optional<NumericCandidateType> numericCandidateTypeFromKey(const Text& candidateType);
WordOrder wordOrderFromKey(const Text& wordOrder);
ByteOrder byteOrderFromKey(const Text& byteOrder);

std::uint16_t swapRegisterBytes(std::uint16_t value);
std::uint16_t normalizeRegisterBytes(std::uint16_t value, ByteOrder order);
std::uint32_t combineWords(std::uint16_t first,
                           std::uint16_t second,
                           WordOrder wordOrder,
                           ByteOrder byteOrder);
float bitsToFloat32(std::uint32_t bits);
std::int16_t toInt16(std::uint16_t value);
std::int32_t toInt32(std::uint32_t value);
std::optional<double> decodePackedBcdWords(const std::vector<std::uint16_t>& registers,
                                           WordOrder wordOrder,
                                           ByteOrder byteOrder);
std::uint16_t gray16ToBinary(std::uint16_t gray);

std::optional<double> decodeNumericValue(NumericCandidateType candidateType,
                                         WordOrder wordOrder,
                                         ByteOrder byteOrder,
                                         const std::vector<std::uint16_t>& registers);
std::optional<double> decodeNumericValue(const Text& candidateType,
                                         const Text& wordOrder,
                                         const Text& byteOrder,
                                         const std::vector<std::uint16_t>& registers);

Text numericCandidateTypeName(NumericCandidateType type);
Text wordOrderName(WordOrder order);
Text byteOrderName(ByteOrder order);
Text endianDescription(WordOrder wordOrder, ByteOrder byteOrder, int registerCount);

CandidateGenerationResult generateValueCandidates(const std::vector<RegisterSample>& samples,
                                                   const TargetValue& target,
                                                   const CandidateGenerationOptions& options = {});

StabilityAnalysisResult analyzeCandidateStability(const std::vector<CandidateObservation>& observations,
                                                   const StabilityAnalysisOptions& options = {});

std::vector<Text> interpretationMapLines(Text text);
std::vector<BitFlagInterpretationDefinition> parseBitFlagInterpretationDefinitions(const Text& text);
std::vector<EnumMapInterpretationDefinition> parseEnumMapInterpretationDefinitions(const Text& text);
Text bitFlagInterpretationText(const Text& text, std::uint16_t mask);
Text enumMapInterpretationText(const Text& text, int value);
InterpretationMapValidationResult validateInterpretationMap(const Text& candidateType, const Text& text);

} // namespace svm::core::analysis
