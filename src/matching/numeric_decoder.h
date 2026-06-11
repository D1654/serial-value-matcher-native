#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace svm::matching {

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

std::optional<NumericCandidateType> numericCandidateTypeFromKey(const QString& candidateType);
WordOrder wordOrderFromKey(const QString& wordOrder);
ByteOrder byteOrderFromKey(const QString& byteOrder);

quint16 swapRegisterBytes(quint16 value);
quint16 normalizeRegisterBytes(quint16 value, ByteOrder order);
quint32 combineWords(quint16 first,
                     quint16 second,
                     WordOrder wordOrder,
                     ByteOrder byteOrder);
float bitsToFloat32(quint32 bits);
qint16 toInt16(quint16 value);
qint32 toInt32(quint32 value);
std::optional<double> decodePackedBcdWords(const QList<quint16>& registers,
                                           WordOrder wordOrder,
                                           ByteOrder byteOrder);
quint16 gray16ToBinary(quint16 gray);

std::optional<double> decodeNumericValue(NumericCandidateType candidateType,
                                         WordOrder wordOrder,
                                         ByteOrder byteOrder,
                                         const QList<quint16>& registers);
std::optional<double> decodeNumericValue(const QString& candidateType,
                                         const QString& wordOrder,
                                         const QString& byteOrder,
                                         const QList<quint16>& registers);

} // namespace svm::matching
