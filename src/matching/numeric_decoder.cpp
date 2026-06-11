#include "matching/numeric_decoder.h"

#include <algorithm>
#include <cstring>

namespace svm::matching {

std::optional<NumericCandidateType> numericCandidateTypeFromKey(const QString& candidateType)
{
    if (candidateType == QStringLiteral("UInt16")) {
        return NumericCandidateType::UInt16;
    }
    if (candidateType == QStringLiteral("Int16")) {
        return NumericCandidateType::Int16;
    }
    if (candidateType == QStringLiteral("UInt32")) {
        return NumericCandidateType::UInt32;
    }
    if (candidateType == QStringLiteral("Int32")) {
        return NumericCandidateType::Int32;
    }
    if (candidateType == QStringLiteral("Float32")) {
        return NumericCandidateType::Float32;
    }
    if (candidateType == QStringLiteral("PackedBCD")) {
        return NumericCandidateType::PackedBCD;
    }
    if (candidateType == QStringLiteral("Gray16")) {
        return NumericCandidateType::Gray16;
    }
    if (candidateType == QStringLiteral("BitFlags") || candidateType == QStringLiteral("EnumMap")) {
        return NumericCandidateType::BitFlags;
    }
    return std::nullopt;
}

WordOrder wordOrderFromKey(const QString& wordOrder)
{
    return wordOrder == QStringLiteral("LowWordFirst") ? WordOrder::LowWordFirst : WordOrder::HighWordFirst;
}

ByteOrder byteOrderFromKey(const QString& byteOrder)
{
    return byteOrder == QStringLiteral("LittleEndian") ? ByteOrder::LittleEndian : ByteOrder::BigEndian;
}

quint16 swapRegisterBytes(quint16 value)
{
    return static_cast<quint16>(((value & 0x00FFu) << 8u) | ((value & 0xFF00u) >> 8u));
}

quint16 normalizeRegisterBytes(quint16 value, ByteOrder order)
{
    return order == ByteOrder::BigEndian ? value : swapRegisterBytes(value);
}

quint32 combineWords(quint16 first,
                     quint16 second,
                     WordOrder wordOrder,
                     ByteOrder byteOrder)
{
    const quint16 normalizedFirst = normalizeRegisterBytes(first, byteOrder);
    const quint16 normalizedSecond = normalizeRegisterBytes(second, byteOrder);
    const quint16 high = wordOrder == WordOrder::HighWordFirst ? normalizedFirst : normalizedSecond;
    const quint16 low = wordOrder == WordOrder::HighWordFirst ? normalizedSecond : normalizedFirst;
    return (static_cast<quint32>(high) << 16u) | static_cast<quint32>(low);
}

float bitsToFloat32(quint32 bits)
{
    static_assert(sizeof(float) == sizeof(quint32));
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

qint16 toInt16(quint16 value)
{
    if (value <= 0x7FFFu) {
        return static_cast<qint16>(value);
    }
    return static_cast<qint16>(static_cast<int>(value) - 0x10000);
}

qint32 toInt32(quint32 value)
{
    if (value <= 0x7FFFFFFFu) {
        return static_cast<qint32>(value);
    }
    return static_cast<qint32>(static_cast<qint64>(value) - 0x100000000LL);
}

std::optional<double> decodePackedBcdWords(const QList<quint16>& registers,
                                           WordOrder wordOrder,
                                           ByteOrder byteOrder)
{
    if (registers.isEmpty()) {
        return std::nullopt;
    }

    QList<quint16> ordered = registers;
    if (ordered.size() > 1 && wordOrder == WordOrder::LowWordFirst) {
        std::reverse(ordered.begin(), ordered.end());
    }

    qulonglong value = 0;
    for (quint16 rawRegister : ordered) {
        const quint16 normalized = normalizeRegisterBytes(rawRegister, byteOrder);
        for (int shift = 12; shift >= 0; shift -= 4) {
            const quint16 digit = static_cast<quint16>((normalized >> shift) & 0x000Fu);
            if (digit > 9u) {
                return std::nullopt;
            }
            value = value * 10u + digit;
        }
    }
    return static_cast<double>(value);
}

quint16 gray16ToBinary(quint16 gray)
{
    quint16 binary = gray;
    for (quint16 shifted = static_cast<quint16>(gray >> 1u); shifted != 0u; shifted = static_cast<quint16>(shifted >> 1u)) {
        binary = static_cast<quint16>(binary ^ shifted);
    }
    return binary;
}

std::optional<double> decodeNumericValue(NumericCandidateType candidateType,
                                         WordOrder wordOrder,
                                         ByteOrder byteOrder,
                                         const QList<quint16>& registers)
{
    switch (candidateType) {
    case NumericCandidateType::UInt16:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(normalizeRegisterBytes(registers.first(), byteOrder));
    case NumericCandidateType::Int16:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(toInt16(normalizeRegisterBytes(registers.first(), byteOrder)));
    case NumericCandidateType::PackedBCD:
        return decodePackedBcdWords(registers, wordOrder, byteOrder);
    case NumericCandidateType::Gray16:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(gray16ToBinary(normalizeRegisterBytes(registers.first(), byteOrder)));
    case NumericCandidateType::BitFlags:
        if (registers.size() != 1) {
            return std::nullopt;
        }
        return static_cast<double>(normalizeRegisterBytes(registers.first(), byteOrder));
    case NumericCandidateType::UInt32:
    case NumericCandidateType::Int32:
    case NumericCandidateType::Float32:
        if (registers.size() != 2) {
            return std::nullopt;
        }
        break;
    }

    const quint32 combined = combineWords(registers.at(0), registers.at(1), wordOrder, byteOrder);
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

std::optional<double> decodeNumericValue(const QString& candidateType,
                                         const QString& wordOrder,
                                         const QString& byteOrder,
                                         const QList<quint16>& registers)
{
    const auto parsedType = numericCandidateTypeFromKey(candidateType);
    if (!parsedType.has_value()) {
        return std::nullopt;
    }
    return decodeNumericValue(*parsedType, wordOrderFromKey(wordOrder), byteOrderFromKey(byteOrder), registers);
}

} // namespace svm::matching
