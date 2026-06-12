#include "core/analysis_core.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace svm::core::analysis {
namespace {

bool isFinite(double value) {
    return std::isfinite(value);
}

double clampScore(double value) {
    if (!isFinite(value)) {
        return 0.0;
    }
    return std::clamp(value, 0.0, 100.0);
}

Text trim(Text value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [isSpace](unsigned char ch) { return !isSpace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [isSpace](unsigned char ch) { return !isSpace(ch); }).base(), value.end());
    return value;
}

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

std::vector<Text> split(const Text& value, char separator, bool keepEmpty) {
    std::vector<Text> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(separator, start);
        Text part = value.substr(start, end == Text::npos ? Text::npos : end - start);
        if (keepEmpty || !part.empty()) {
            parts.push_back(std::move(part));
        }
        if (end == Text::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

Text join(const std::vector<Text>& values, const Text& separator) {
    Text result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            result += separator;
        }
        result += values[index];
    }
    return result;
}

Text number(double value, int precision = 12) {
    std::ostringstream stream;
    stream << std::setprecision(precision) << value;
    return stream.str();
}

Text bitLabel(int bitIndex) {
    return "bit" + std::to_string(bitIndex);
}

bool startsWithBitPrefix(Text value) {
    value = trim(std::move(value));
    if (value.size() < 3) {
        return false;
    }
    return (value[0] == 'b' || value[0] == 'B')
        && (value[1] == 'i' || value[1] == 'I')
        && (value[2] == 't' || value[2] == 'T');
}

Text bitKeyText(Text keyText) {
    keyText = trim(std::move(keyText));
    if (startsWithBitPrefix(keyText)) {
        keyText = trim(keyText.substr(3));
    }
    return keyText;
}

bool parseInteger(Text text, int* value) {
    text = trim(std::move(text));
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        if (value != nullptr) {
            *value = parsed;
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<Text> previewHead(const std::vector<Text>& parts, int maxCount = 6) {
    std::vector<Text> preview;
    for (int index = 0; index < static_cast<int>(parts.size()) && index < maxCount; ++index) {
        preview.push_back(parts[static_cast<std::size_t>(index)]);
    }
    if (static_cast<int>(parts.size()) > maxCount) {
        preview.push_back("……共 " + std::to_string(parts.size()) + " 项");
    }
    return preview;
}

bool sameSeries(const RegisterSample& left, const RegisterSample& right) {
    return left.sessionId == right.sessionId
        && left.slaveId == right.slaveId
        && left.functionCode == right.functionCode;
}

double effectiveToleranceFor(const TargetValue& target, const MatchTolerance& tolerance) {
    return std::max(tolerance.absolute, std::abs(target.value) * tolerance.relativeRatio);
}

bool withinTolerance(double absoluteError, double effectiveTolerance) {
    if (effectiveTolerance <= 0.0) {
        return absoluteError == 0.0;
    }
    return absoluteError <= effectiveTolerance;
}

double typePrior(NumericCandidateType type) {
    switch (type) {
    case NumericCandidateType::UInt16:
        return 1.00;
    case NumericCandidateType::Int16:
        return 0.98;
    case NumericCandidateType::UInt32:
        return 0.92;
    case NumericCandidateType::Int32:
        return 0.90;
    case NumericCandidateType::Float32:
        return 0.85;
    case NumericCandidateType::PackedBCD:
        return 0.96;
    case NumericCandidateType::Gray16:
        return 0.94;
    case NumericCandidateType::BitFlags:
        return 0.88;
    }
    return 0.80;
}

double endianPrior(WordOrder wordOrder, ByteOrder byteOrder, int registerCount) {
    if (registerCount == 1) {
        return byteOrder == ByteOrder::BigEndian ? 1.00 : 0.90;
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::BigEndian) {
        return 1.00;
    }
    if (wordOrder == WordOrder::LowWordFirst && byteOrder == ByteOrder::BigEndian) {
        return 0.95;
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::LittleEndian) {
        return 0.90;
    }
    return 0.85;
}

double scalePrior(const ScaleTransform& scale) {
    if (scale.offset != 0.0) {
        return 0.85;
    }
    if (scale.multiplier == 1.0) {
        return 1.00;
    }
    if (scale.multiplier == 0.1 || scale.multiplier == 0.01 || scale.multiplier == 0.001 || scale.multiplier == 0.0001) {
        return 0.95;
    }
    if (scale.multiplier == 10.0 || scale.multiplier == 100.0 || scale.multiplier == 1000.0) {
        return 0.90;
    }
    return 0.80;
}

double scoreFor(NumericCandidateType type,
                WordOrder wordOrder,
                ByteOrder byteOrder,
                int registerCount,
                const ScaleTransform& scale,
                double absoluteError,
                double effectiveTolerance) {
    const double matchQuality = effectiveTolerance <= 0.0 ? 1.0 : std::max(0.0, 1.0 - (absoluteError / effectiveTolerance));
    const double baseScore = 60.0 + 40.0 * matchQuality;
    return baseScore * typePrior(type) * endianPrior(wordOrder, byteOrder, registerCount) * scalePrior(scale);
}

void appendSource(ValueMatchCandidate& candidate, const RegisterSample& sample) {
    candidate.observationIds.push_back(sample.observationId);
    candidate.addresses.push_back(sample.address);
    candidate.blockIndexes.push_back(sample.blockIndex);
    candidate.attemptIndexes.push_back(sample.attemptIndex);
    candidate.rawRegisters.push_back(sample.value);
}

bool standardEndianBefore(const ValueMatchCandidate& left, const ValueMatchCandidate& right) {
    const double leftPrior = endianPrior(left.wordOrder, left.byteOrder, left.registerCount);
    const double rightPrior = endianPrior(right.wordOrder, right.byteOrder, right.registerCount);
    return leftPrior > rightPrior;
}

bool candidateRankedBefore(const ValueMatchCandidate& left, const ValueMatchCandidate& right) {
    if (left.score != right.score) {
        return left.score > right.score;
    }
    if (left.absoluteError != right.absoluteError) {
        return left.absoluteError < right.absoluteError;
    }
    if (left.registerCount != right.registerCount) {
        return left.registerCount < right.registerCount;
    }
    if (standardEndianBefore(left, right) != standardEndianBefore(right, left)) {
        return standardEndianBefore(left, right);
    }
    if (left.startAddress != right.startAddress) {
        return left.startAddress < right.startAddress;
    }
    return numericCandidateTypeName(left.type) < numericCandidateTypeName(right.type);
}

void removeWorstCandidate(std::vector<ValueMatchCandidate>& candidates) {
    auto worst = candidates.begin();
    for (auto current = candidates.begin() + 1; current != candidates.end(); ++current) {
        if (candidateRankedBefore(*worst, *current)) {
            worst = current;
        }
    }
    candidates.erase(worst);
}

void appendBoundedCandidate(std::vector<ValueMatchCandidate>& candidates,
                            const ValueMatchCandidate& candidate,
                            int maxCandidates) {
    candidates.push_back(candidate);
    if (static_cast<int>(candidates.size()) > maxCandidates) {
        removeWorstCandidate(candidates);
    }
}

void addCandidate(std::vector<ValueMatchCandidate>& candidates,
                  int maxCandidates,
                  const TargetValue& target,
                  double effectiveTolerance,
                  NumericCandidateType type,
                  WordOrder wordOrder,
                  ByteOrder byteOrder,
                  const RegisterSample& first,
                  const RegisterSample* second,
                  double decodedValue,
                  const ScaleTransform& scale) {
    if (!isFinite(decodedValue) || !isFinite(scale.multiplier) || !isFinite(scale.offset) || scale.multiplier == 0.0) {
        return;
    }

    const double engineeringValue = decodedValue * scale.multiplier + scale.offset;
    if (!isFinite(engineeringValue)) {
        return;
    }

    const double delta = engineeringValue - target.value;
    const double absoluteError = std::abs(delta);
    if (!withinTolerance(absoluteError, effectiveTolerance)) {
        return;
    }

    ValueMatchCandidate candidate;
    candidate.type = type;
    candidate.wordOrder = wordOrder;
    candidate.byteOrder = byteOrder;
    candidate.sessionId = first.sessionId;
    candidate.slaveId = first.slaveId;
    candidate.functionCode = first.functionCode;
    candidate.startAddress = first.address;
    candidate.registerCount = second == nullptr ? 1 : 2;
    appendSource(candidate, first);
    if (second != nullptr) {
        appendSource(candidate, *second);
    }
    candidate.decodedValue = decodedValue;
    candidate.scale = scale;
    candidate.engineeringValue = engineeringValue;
    candidate.delta = delta;
    candidate.absoluteError = absoluteError;
    candidate.effectiveTolerance = effectiveTolerance;
    candidate.score = scoreFor(type, wordOrder, byteOrder, candidate.registerCount, scale, absoluteError, effectiveTolerance);
    candidate.evidenceText = "单样本候选：地址 " + std::to_string(candidate.startAddress)
        + "，" + numericCandidateTypeName(type)
        + "，" + endianDescription(wordOrder, byteOrder, candidate.registerCount)
        + "，解码值 " + number(candidate.decodedValue)
        + "，工程值 " + number(candidate.engineeringValue)
        + "，误差 " + number(candidate.absoluteError);
    appendBoundedCandidate(candidates, candidate, maxCandidates);
}

std::vector<ScaleTransform> normalizedScaleTransforms(const std::vector<ScaleTransform>& transforms) {
    std::vector<ScaleTransform> result;
    for (const ScaleTransform& transform : transforms) {
        if (!isFinite(transform.multiplier) || !isFinite(transform.offset) || transform.multiplier == 0.0) {
            continue;
        }
        const bool exists = std::any_of(result.cbegin(), result.cend(), [&transform](const ScaleTransform& existing) {
            return existing.multiplier == transform.multiplier && existing.offset == transform.offset;
        });
        if (!exists) {
            result.push_back(transform);
        }
    }
    return result;
}

Text identityKey(const CandidateObservation& observation) {
    std::ostringstream stream;
    stream << observation.candidateType << '|'
           << observation.wordOrder << '|'
           << observation.byteOrder << '|'
           << observation.slaveId << '|'
           << observation.functionCode << '|'
           << observation.startAddress << '|'
           << observation.registerCount << '|'
           << std::setprecision(17) << observation.scaleMultiplier << '|'
           << std::setprecision(17) << observation.scaleOffset;
    return stream.str();
}

template <typename T>
void appendUnique(std::vector<T>& target, const T& value) {
    if (std::find(target.cbegin(), target.cend(), value) == target.cend()) {
        target.push_back(value);
    }
}

double qualityForError(double absoluteError, double effectiveTolerance) {
    if (!isFinite(absoluteError) || absoluteError < 0.0) {
        return 0.0;
    }
    if (!isFinite(effectiveTolerance) || effectiveTolerance < 0.0) {
        return 0.0;
    }
    if (effectiveTolerance == 0.0) {
        return absoluteError == 0.0 ? 100.0 : 0.0;
    }
    return clampScore(100.0 * (1.0 - (absoluteError / effectiveTolerance)));
}

double qualityForSampleCount(int sampleCount, const StabilityAnalysisOptions& options) {
    const int minimum = std::max(1, options.minimumSampleCount);
    const int strong = std::max(minimum, options.strongSampleCount);
    if (sampleCount < minimum) {
        return clampScore(30.0 * static_cast<double>(sampleCount) / static_cast<double>(minimum));
    }
    if (strong == minimum) {
        return 100.0;
    }
    const double progress = static_cast<double>(sampleCount - minimum) / static_cast<double>(strong - minimum);
    return clampScore(70.0 + 30.0 * progress);
}

Text confidenceFor(double stabilityScore, int sampleCount, int minimumSampleCount) {
    if (sampleCount < minimumSampleCount) {
        return "低";
    }
    if (stabilityScore >= 85.0) {
        return "高";
    }
    if (stabilityScore >= 65.0) {
        return "中";
    }
    return "低";
}

} // namespace

std::optional<NumericCandidateType> numericCandidateTypeFromKey(const Text& candidateType) {
    if (candidateType == "UInt16") {
        return NumericCandidateType::UInt16;
    }
    if (candidateType == "Int16") {
        return NumericCandidateType::Int16;
    }
    if (candidateType == "UInt32") {
        return NumericCandidateType::UInt32;
    }
    if (candidateType == "Int32") {
        return NumericCandidateType::Int32;
    }
    if (candidateType == "Float32") {
        return NumericCandidateType::Float32;
    }
    if (candidateType == "PackedBCD") {
        return NumericCandidateType::PackedBCD;
    }
    if (candidateType == "Gray16") {
        return NumericCandidateType::Gray16;
    }
    if (candidateType == "BitFlags" || candidateType == "EnumMap") {
        return NumericCandidateType::BitFlags;
    }
    return std::nullopt;
}

WordOrder wordOrderFromKey(const Text& wordOrder) {
    return wordOrder == "LowWordFirst" ? WordOrder::LowWordFirst : WordOrder::HighWordFirst;
}

ByteOrder byteOrderFromKey(const Text& byteOrder) {
    return byteOrder == "LittleEndian" ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
}

std::uint16_t swapRegisterBytes(std::uint16_t value) {
    return static_cast<std::uint16_t>(((value & 0x00FFu) << 8u) | ((value & 0xFF00u) >> 8u));
}

std::uint16_t normalizeRegisterBytes(std::uint16_t value, ByteOrder order) {
    return order == ByteOrder::BigEndian ? value : swapRegisterBytes(value);
}

std::uint32_t combineWords(std::uint16_t first,
                           std::uint16_t second,
                           WordOrder wordOrder,
                           ByteOrder byteOrder) {
    const std::uint16_t normalizedFirst = normalizeRegisterBytes(first, byteOrder);
    const std::uint16_t normalizedSecond = normalizeRegisterBytes(second, byteOrder);
    const std::uint16_t high = wordOrder == WordOrder::HighWordFirst ? normalizedFirst : normalizedSecond;
    const std::uint16_t low = wordOrder == WordOrder::HighWordFirst ? normalizedSecond : normalizedFirst;
    return (static_cast<std::uint32_t>(high) << 16u) | static_cast<std::uint32_t>(low);
}

float bitsToFloat32(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::int16_t toInt16(std::uint16_t value) {
    if (value <= 0x7FFFu) {
        return static_cast<std::int16_t>(value);
    }
    return static_cast<std::int16_t>(static_cast<int>(value) - 0x10000);
}

std::int32_t toInt32(std::uint32_t value) {
    if (value <= 0x7FFFFFFFu) {
        return static_cast<std::int32_t>(value);
    }
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000LL);
}

std::optional<double> decodePackedBcdWords(const std::vector<std::uint16_t>& registers,
                                           WordOrder wordOrder,
                                           ByteOrder byteOrder) {
    if (registers.empty()) {
        return std::nullopt;
    }

    std::vector<std::uint16_t> ordered = registers;
    if (ordered.size() > 1 && wordOrder == WordOrder::LowWordFirst) {
        std::reverse(ordered.begin(), ordered.end());
    }

    std::uint64_t value = 0;
    for (std::uint16_t rawRegister : ordered) {
        const std::uint16_t normalized = normalizeRegisterBytes(rawRegister, byteOrder);
        for (int shift = 12; shift >= 0; shift -= 4) {
            const std::uint16_t digit = static_cast<std::uint16_t>((normalized >> shift) & 0x000Fu);
            if (digit > 9u) {
                return std::nullopt;
            }
            value = value * 10u + digit;
        }
    }
    return static_cast<double>(value);
}

std::uint16_t gray16ToBinary(std::uint16_t gray) {
    std::uint16_t binary = gray;
    for (std::uint16_t shifted = static_cast<std::uint16_t>(gray >> 1u); shifted != 0u; shifted = static_cast<std::uint16_t>(shifted >> 1u)) {
        binary = static_cast<std::uint16_t>(binary ^ shifted);
    }
    return binary;
}

std::optional<double> decodeNumericValue(NumericCandidateType candidateType,
                                         WordOrder wordOrder,
                                         ByteOrder byteOrder,
                                         const std::vector<std::uint16_t>& registers) {
    switch (candidateType) {
    case NumericCandidateType::UInt16:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(normalizeRegisterBytes(registers.front(), byteOrder));
    case NumericCandidateType::Int16:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(toInt16(normalizeRegisterBytes(registers.front(), byteOrder)));
    case NumericCandidateType::PackedBCD:
        return decodePackedBcdWords(registers, wordOrder, byteOrder);
    case NumericCandidateType::Gray16:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(gray16ToBinary(normalizeRegisterBytes(registers.front(), byteOrder)));
    case NumericCandidateType::BitFlags:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(normalizeRegisterBytes(registers.front(), byteOrder));
    case NumericCandidateType::UInt32:
    case NumericCandidateType::Int32:
    case NumericCandidateType::Float32:
        if (registers.size() != 2) {
            return std::nullopt;
        }
        break;
    }

    const std::uint32_t combined = combineWords(registers[0], registers[1], wordOrder, byteOrder);
    switch (candidateType) {
    case NumericCandidateType::UInt32:
        return static_cast<double>(combined);
    case NumericCandidateType::Int32:
        return static_cast<double>(toInt32(combined));
    case NumericCandidateType::Float32:
        return static_cast<double>(bitsToFloat32(combined));
    case NumericCandidateType::UInt16:
    case NumericCandidateType::Int16:
    case NumericCandidateType::PackedBCD:
    case NumericCandidateType::Gray16:
    case NumericCandidateType::BitFlags:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<double> decodeNumericValue(const Text& candidateType,
                                         const Text& wordOrder,
                                         const Text& byteOrder,
                                         const std::vector<std::uint16_t>& registers) {
    const auto parsedType = numericCandidateTypeFromKey(candidateType);
    if (!parsedType.has_value()) {
        return std::nullopt;
    }
    return decodeNumericValue(*parsedType, wordOrderFromKey(wordOrder), byteOrderFromKey(byteOrder), registers);
}

Text numericCandidateTypeName(NumericCandidateType type) {
    switch (type) {
    case NumericCandidateType::UInt16:
        return "UInt16";
    case NumericCandidateType::Int16:
        return "Int16";
    case NumericCandidateType::UInt32:
        return "UInt32";
    case NumericCandidateType::Int32:
        return "Int32";
    case NumericCandidateType::Float32:
        return "Float32";
    case NumericCandidateType::PackedBCD:
        return "PackedBCD";
    case NumericCandidateType::Gray16:
        return "Gray16";
    case NumericCandidateType::BitFlags:
        return "BitFlags";
    }
    return "未知数值类型";
}

Text wordOrderName(WordOrder order) {
    switch (order) {
    case WordOrder::HighWordFirst:
        return "高字在前";
    case WordOrder::LowWordFirst:
        return "低字在前";
    }
    return "未知字序";
}

Text byteOrderName(ByteOrder order) {
    switch (order) {
    case ByteOrder::BigEndian:
        return "寄存器内高字节在前";
    case ByteOrder::LittleEndian:
        return "寄存器内低字节在前";
    }
    return "未知字节序";
}

Text endianDescription(WordOrder wordOrder, ByteOrder byteOrder, int registerCount) {
    if (registerCount == 1) {
        return byteOrder == ByteOrder::BigEndian ? "标准字节序" : "寄存器内字节交换";
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::BigEndian) {
        return "标准字节序";
    }
    if (wordOrder == WordOrder::LowWordFirst && byteOrder == ByteOrder::BigEndian) {
        return "word swap";
    }
    if (wordOrder == WordOrder::HighWordFirst && byteOrder == ByteOrder::LittleEndian) {
        return "寄存器内字节交换";
    }
    return "word swap + 字节交换";
}

CandidateGenerationResult generateValueCandidates(const std::vector<RegisterSample>& samples,
                                                   const TargetValue& target,
                                                   const CandidateGenerationOptions& options) {
    CandidateGenerationResult result;

    if (samples.empty()) {
        result.errorMessage = "没有可用于候选生成的寄存器观测";
        return result;
    }
    if (!isFinite(target.value)) {
        result.errorMessage = "目标值不是有效数字";
        return result;
    }
    if (!isFinite(options.tolerance.absolute) || options.tolerance.absolute < 0.0
        || !isFinite(options.tolerance.relativeRatio) || options.tolerance.relativeRatio < 0.0) {
        result.errorMessage = "容差必须是大于等于 0 的有效数字";
        return result;
    }
    if (options.maxCandidates <= 0) {
        result.errorMessage = "候选数量上限必须大于 0";
        return result;
    }

    const std::vector<ScaleTransform> scales = normalizedScaleTransforms(options.scaleTransforms);
    if (scales.empty()) {
        result.errorMessage = "至少需要一个有效倍率";
        return result;
    }

    const double effectiveTolerance = effectiveToleranceFor(target, options.tolerance);
    std::vector<RegisterSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(), [](const RegisterSample& left, const RegisterSample& right) {
        if (left.sessionId != right.sessionId) {
            return left.sessionId < right.sessionId;
        }
        if (left.slaveId != right.slaveId) {
            return left.slaveId < right.slaveId;
        }
        if (left.functionCode != right.functionCode) {
            return left.functionCode < right.functionCode;
        }
        return left.address < right.address;
    });

    std::vector<ValueMatchCandidate> candidates;
    const std::vector<ByteOrder> byteOrders = {ByteOrder::BigEndian, ByteOrder::LittleEndian};
    const std::vector<WordOrder> wordOrders = {WordOrder::HighWordFirst, WordOrder::LowWordFirst};

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const RegisterSample& first = sorted[i];
        for (ByteOrder byteOrder : byteOrders) {
            const std::uint16_t single = normalizeRegisterBytes(first.value, byteOrder);
            for (const ScaleTransform& scale : scales) {
                if (options.includeUInt16) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::UInt16, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(single), scale);
                }
                if (options.includeInt16) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Int16, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(toInt16(single)), scale);
                }
                if (options.includePackedBCD) {
                    const auto bcdValue = decodePackedBcdWords({first.value}, WordOrder::HighWordFirst, byteOrder);
                    if (bcdValue.has_value()) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::PackedBCD, WordOrder::HighWordFirst, byteOrder, first, nullptr, *bcdValue, scale);
                    }
                }
                if (options.includeGray16) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Gray16, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(gray16ToBinary(single)), scale);
                }
                if (options.includeBitFlags) {
                    addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::BitFlags, WordOrder::HighWordFirst, byteOrder, first, nullptr, static_cast<double>(single), scale);
                }
            }
        }

        if (i + 1 >= sorted.size()) {
            continue;
        }
        const RegisterSample& second = sorted[i + 1];
        if (!sameSeries(first, second) || second.address != first.address + 1) {
            continue;
        }

        for (WordOrder wordOrder : wordOrders) {
            for (ByteOrder byteOrder : byteOrders) {
                const std::uint32_t combined = combineWords(first.value, second.value, wordOrder, byteOrder);
                const double uint32Value = static_cast<double>(combined);
                const double int32Value = static_cast<double>(toInt32(combined));
                const double float32Value = static_cast<double>(bitsToFloat32(combined));
                const auto packedBcdValue = decodePackedBcdWords({first.value, second.value}, wordOrder, byteOrder);
                for (const ScaleTransform& scale : scales) {
                    if (options.includeUInt32) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::UInt32, wordOrder, byteOrder, first, &second, uint32Value, scale);
                    }
                    if (options.includeInt32) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Int32, wordOrder, byteOrder, first, &second, int32Value, scale);
                    }
                    if (options.includeFloat32) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::Float32, wordOrder, byteOrder, first, &second, float32Value, scale);
                    }
                    if (options.includePackedBCD && packedBcdValue.has_value()) {
                        addCandidate(candidates, options.maxCandidates, target, effectiveTolerance, NumericCandidateType::PackedBCD, wordOrder, byteOrder, first, &second, *packedBcdValue, scale);
                    }
                }
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), candidateRankedBefore);
    while (static_cast<int>(candidates.size()) > options.maxCandidates) {
        candidates.pop_back();
    }

    result.success = true;
    result.candidates = std::move(candidates);
    return result;
}

StabilityAnalysisResult analyzeCandidateStability(const std::vector<CandidateObservation>& observations,
                                                   const StabilityAnalysisOptions& options) {
    StabilityAnalysisResult result;

    if (observations.empty()) {
        result.errorMessage = "没有可用于稳定性分析的候选观测";
        return result;
    }
    if (options.minimumSampleCount <= 0 || options.strongSampleCount <= 0) {
        result.errorMessage = "样本数量阈值必须大于 0";
        return result;
    }

    std::map<Text, std::vector<CandidateObservation>> groups;
    for (const CandidateObservation& observation : observations) {
        if (observation.candidateType.empty()) {
            result.errorMessage = "候选类型不能为空";
            return result;
        }
        if (!isFinite(observation.targetValue) || !isFinite(observation.engineeringValue)
            || !isFinite(observation.absoluteError) || !isFinite(observation.effectiveTolerance)
            || !isFinite(observation.candidateScore)) {
            result.errorMessage = "候选观测包含无效数字";
            return result;
        }
        groups[identityKey(observation)].push_back(observation);
    }

    const int minimum = std::max(1, options.minimumSampleCount);
    for (const auto& [_, group] : groups) {
        if (group.empty()) {
            continue;
        }

        const CandidateObservation& first = group.front();
        StableCandidate stable;
        stable.candidateType = first.candidateType;
        stable.wordOrder = first.wordOrder;
        stable.byteOrder = first.byteOrder;
        stable.slaveId = first.slaveId;
        stable.functionCode = first.functionCode;
        stable.startAddress = first.startAddress;
        stable.registerCount = first.registerCount;
        stable.scaleMultiplier = first.scaleMultiplier;
        stable.scaleOffset = first.scaleOffset;
        stable.sampleCount = static_cast<int>(group.size());
        stable.meetsMinimumSampleCount = stable.sampleCount >= minimum;

        double targetSum = 0.0;
        double engineeringSum = 0.0;
        double errorSum = 0.0;
        double qualitySum = 0.0;
        double scoreSum = 0.0;
        double maxError = 0.0;

        for (const CandidateObservation& observation : group) {
            appendUnique(stable.runIds, observation.runId);
            appendUnique(stable.sourceScanSessionIds, observation.sourceScanSessionId);
            for (const std::int64_t id : observation.observationIds) {
                appendUnique(stable.observationIds, id);
            }
            for (const int address : observation.addresses) {
                appendUnique(stable.addresses, address);
            }

            targetSum += observation.targetValue;
            engineeringSum += observation.engineeringValue;
            errorSum += observation.absoluteError;
            qualitySum += qualityForError(observation.absoluteError, observation.effectiveTolerance);
            scoreSum += clampScore(observation.candidateScore);
            maxError = std::max(maxError, observation.absoluteError);
        }

        stable.meanTargetValue = targetSum / stable.sampleCount;
        stable.meanEngineeringValue = engineeringSum / stable.sampleCount;
        stable.meanAbsoluteError = errorSum / stable.sampleCount;
        stable.maxAbsoluteError = maxError;
        stable.meanCandidateScore = scoreSum / stable.sampleCount;
        stable.meanErrorQuality = qualitySum / stable.sampleCount;
        stable.sampleQuality = qualityForSampleCount(stable.sampleCount, options);
        stable.stabilityScore = clampScore(0.45 * stable.meanCandidateScore
                                           + 0.40 * stable.meanErrorQuality
                                           + 0.15 * stable.sampleQuality);
        if (!stable.meetsMinimumSampleCount) {
            stable.stabilityScore = std::min(stable.stabilityScore, 59.0);
        }
        stable.confidenceLevel = confidenceFor(stable.stabilityScore, stable.sampleCount, minimum);
        stable.evidenceSummary = std::to_string(stable.sampleCount)
            + " 次样本，平均误差 " + number(stable.meanAbsoluteError)
            + "，最大误差 " + number(stable.maxAbsoluteError)
            + "，稳定性评分 " + number(stable.stabilityScore, 2)
            + "，置信等级 " + stable.confidenceLevel;
        result.candidates.push_back(std::move(stable));
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const StableCandidate& left, const StableCandidate& right) {
        if (left.stabilityScore != right.stabilityScore) {
            return left.stabilityScore > right.stabilityScore;
        }
        if (left.sampleCount != right.sampleCount) {
            return left.sampleCount > right.sampleCount;
        }
        if (left.meanAbsoluteError != right.meanAbsoluteError) {
            return left.meanAbsoluteError < right.meanAbsoluteError;
        }
        return left.startAddress < right.startAddress;
    });

    result.success = true;
    return result;
}

std::vector<Text> interpretationMapLines(Text text) {
    text = replaceAll(std::move(text), ";", "\n");
    std::vector<Text> lines;
    for (Text rawLine : split(text, '\n', false)) {
        Text line = trim(std::move(rawLine));
        if (!line.empty() && line.front() != '#') {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

std::vector<BitFlagInterpretationDefinition> parseBitFlagInterpretationDefinitions(const Text& text) {
    std::vector<BitFlagInterpretationDefinition> definitions;
    for (const Text& line : interpretationMapLines(text)) {
        const std::size_t separator = line.find('=');
        if (separator == Text::npos || separator == 0) {
            continue;
        }

        int bitIndex = -1;
        if (!parseInteger(bitKeyText(line.substr(0, separator)), &bitIndex) || bitIndex < 0 || bitIndex > 15) {
            continue;
        }

        const std::vector<Text> parts = split(line.substr(separator + 1), '|', true);
        BitFlagInterpretationDefinition definition;
        definition.bitIndex = bitIndex;
        definition.name = parts.empty() ? "" : trim(parts[0]);
        definition.inactiveText = parts.size() < 2 ? "" : trim(parts[1]);
        definition.activeText = parts.size() < 3 ? "" : trim(parts[2]);
        if (definition.name.empty()) {
            definition.name = bitLabel(bitIndex);
        }
        if (definition.inactiveText.empty()) {
            definition.inactiveText = "未置位";
        }
        if (definition.activeText.empty()) {
            definition.activeText = "置位";
        }
        definitions.push_back(std::move(definition));
    }

    std::sort(definitions.begin(), definitions.end(), [](const BitFlagInterpretationDefinition& left, const BitFlagInterpretationDefinition& right) {
        return left.bitIndex < right.bitIndex;
    });
    return definitions;
}

std::vector<EnumMapInterpretationDefinition> parseEnumMapInterpretationDefinitions(const Text& text) {
    std::vector<EnumMapInterpretationDefinition> definitions;
    for (const Text& line : interpretationMapLines(text)) {
        const std::size_t separator = line.find('=');
        if (separator == Text::npos || separator == 0) {
            continue;
        }

        int value = 0;
        if (!parseInteger(line.substr(0, separator), &value)) {
            continue;
        }

        Text label = trim(line.substr(separator + 1));
        if (label.empty()) {
            continue;
        }

        definitions.push_back(EnumMapInterpretationDefinition{value, std::move(label)});
    }

    std::sort(definitions.begin(), definitions.end(), [](const EnumMapInterpretationDefinition& left, const EnumMapInterpretationDefinition& right) {
        return left.value < right.value;
    });
    return definitions;
}

Text bitFlagInterpretationText(const Text& text, std::uint16_t mask) {
    const std::vector<BitFlagInterpretationDefinition> definitions = parseBitFlagInterpretationDefinitions(text);
    if (definitions.empty()) {
        return {};
    }

    std::vector<Text> parts;
    parts.reserve(definitions.size());
    for (const BitFlagInterpretationDefinition& definition : definitions) {
        const bool active = ((mask >> definition.bitIndex) & 0x0001u) != 0u;
        parts.push_back("bit" + std::to_string(definition.bitIndex) + " " + definition.name + "="
            + (active ? definition.activeText : definition.inactiveText));
    }
    return "位解释：" + join(parts, "；") + "。";
}

Text enumMapInterpretationText(const Text& text, int value) {
    if (trim(text).empty()) {
        return {};
    }

    const std::vector<EnumMapInterpretationDefinition> definitions = parseEnumMapInterpretationDefinitions(text);
    for (const EnumMapInterpretationDefinition& definition : definitions) {
        if (definition.value == value) {
            return "枚举解释：" + definition.label + "。";
        }
    }

    return definitions.empty() ? Text{} : "枚举解释：未定义枚举值 " + std::to_string(value) + "。";
}

InterpretationMapValidationResult validateInterpretationMap(const Text& candidateType, const Text& text) {
    InterpretationMapValidationResult result;
    const Text trimmedText = trim(text);
    if (trimmedText.empty()) {
        result.previewText = "未填写解释映射；验证时只显示原始数值。";
        return result;
    }

    if (candidateType == "BitFlags") {
        std::unordered_set<int> seenBits;
        std::vector<Text> previewParts;
        for (const Text& line : interpretationMapLines(trimmedText)) {
            const std::size_t separator = line.find('=');
            if (separator == Text::npos || separator == 0) {
                result.errors.push_back("BitFlags 映射缺少等号：" + line);
                continue;
            }

            int bitIndex = -1;
            if (!parseInteger(bitKeyText(line.substr(0, separator)), &bitIndex) || bitIndex < 0 || bitIndex > 15) {
                result.errors.push_back("BitFlags 位号必须是 0-15：" + line);
                continue;
            }
            if (seenBits.contains(bitIndex)) {
                result.errors.push_back("BitFlags 位号重复：bit" + std::to_string(bitIndex));
                continue;
            }
            seenBits.insert(bitIndex);

            const std::vector<Text> parts = split(line.substr(separator + 1), '|', true);
            const Text name = parts.empty() || trim(parts[0]).empty() ? bitLabel(bitIndex) : trim(parts[0]);
            const Text inactiveText = parts.size() < 2 || trim(parts[1]).empty() ? "未置位" : trim(parts[1]);
            const Text activeText = parts.size() < 3 || trim(parts[2]).empty() ? "置位" : trim(parts[2]);
            previewParts.push_back("bit" + std::to_string(bitIndex) + " " + name + "（0=" + inactiveText + "，1=" + activeText + "）");
        }

        result.definitionCount = static_cast<int>(seenBits.size());
        result.valid = result.errors.empty();
        result.previewText = previewParts.empty()
            ? "未识别到有效 BitFlags 位定义。"
            : "已识别 " + std::to_string(previewParts.size()) + " 个位定义：" + join(previewHead(previewParts), "；");
        return result;
    }

    if (candidateType == "EnumMap") {
        std::unordered_set<int> seenValues;
        std::vector<Text> previewParts;
        for (const Text& line : interpretationMapLines(trimmedText)) {
            const std::size_t separator = line.find('=');
            if (separator == Text::npos || separator == 0) {
                result.errors.push_back("EnumMap 映射缺少等号：" + line);
                continue;
            }

            int value = 0;
            if (!parseInteger(line.substr(0, separator), &value)) {
                result.errors.push_back("EnumMap 枚举值必须是整数：" + line);
                continue;
            }
            if (seenValues.contains(value)) {
                result.errors.push_back("EnumMap 枚举值重复：" + std::to_string(value));
                continue;
            }
            seenValues.insert(value);

            const Text label = trim(line.substr(separator + 1));
            if (label.empty()) {
                result.errors.push_back("EnumMap 枚举含义不能为空：" + line);
                continue;
            }
            previewParts.push_back(std::to_string(value) + "=" + label);
        }

        result.definitionCount = static_cast<int>(previewParts.size());
        result.valid = result.errors.empty();
        result.previewText = previewParts.empty()
            ? "未识别到有效 EnumMap 枚举定义。"
            : "已识别 " + std::to_string(previewParts.size()) + " 个枚举值：" + join(previewHead(previewParts), "；");
        return result;
    }

    result.previewText = "当前规则类型不会使用解释映射；保存后解释文本不会参与验证。请切换到 BitFlags 或 EnumMap 后再填写。或清空解释映射。 ";
    return result;
}

} // namespace svm::core::analysis
