#pragma once

#include <cstdint>
#include <string>

namespace svm::win32 {

class NativeStatusCountersState final {
public:
    std::uint64_t txBytes() const noexcept;
    std::uint64_t rxBytes() const noexcept;

    void addTxBytes(std::uint64_t byteCount) noexcept;
    void addRxBytes(std::uint64_t byteCount) noexcept;
    void reset() noexcept;

    std::wstring txStatusText() const;
    std::wstring rxStatusText() const;

private:
    std::uint64_t txByteCount_ = 0;
    std::uint64_t rxByteCount_ = 0;
};

} // namespace svm::win32
