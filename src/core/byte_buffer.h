#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace svm::core {

using Byte = std::uint8_t;
using ByteBuffer = std::vector<Byte>;
using ByteSpan = std::span<const Byte>;

} // namespace svm::core
