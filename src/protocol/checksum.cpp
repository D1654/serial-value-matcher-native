#include "protocol/checksum.h"

namespace svm::protocol {

quint8 sum8(const QByteArray& data) {
    quint8 sum = 0;
    for (const unsigned char byte : data) {
        sum = static_cast<quint8>(sum + byte);
    }
    return sum;
}

quint8 xor8(const QByteArray& data) {
    quint8 result = 0;
    for (const unsigned char byte : data) {
        result = static_cast<quint8>(result ^ byte);
    }
    return result;
}

quint8 lrc8(const QByteArray& data) {
    return static_cast<quint8>(0u - sum8(data));
}

} // namespace svm::protocol
