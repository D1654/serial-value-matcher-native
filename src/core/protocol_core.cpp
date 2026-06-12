#include "core/protocol_core.h"

namespace svm::core::protocol {

std::uint8_t sum8(ByteSpan data) {
    std::uint8_t sum = 0;
    for (const Byte byte : data) {
        sum = static_cast<std::uint8_t>(sum + byte);
    }
    return sum;
}

std::uint8_t xor8(ByteSpan data) {
    std::uint8_t result = 0;
    for (const Byte byte : data) {
        result = static_cast<std::uint8_t>(result ^ byte);
    }
    return result;
}

std::uint8_t lrc8(ByteSpan data) {
    return static_cast<std::uint8_t>(0u - sum8(data));
}

} // namespace svm::core::protocol
