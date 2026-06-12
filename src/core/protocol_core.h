#pragma once

#include "core/byte_buffer.h"

#include <cstdint>

namespace svm::core::protocol {

std::uint8_t sum8(ByteSpan data);
std::uint8_t xor8(ByteSpan data);
std::uint8_t lrc8(ByteSpan data);

} // namespace svm::core::protocol
