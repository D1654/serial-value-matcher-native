#include "win32/native_status_counters_state.h"

namespace svm::win32 {

std::uint64_t NativeStatusCountersState::txBytes() const noexcept {
    return txByteCount_;
}

std::uint64_t NativeStatusCountersState::rxBytes() const noexcept {
    return rxByteCount_;
}

void NativeStatusCountersState::addTxBytes(std::uint64_t byteCount) noexcept {
    txByteCount_ += byteCount;
}

void NativeStatusCountersState::addRxBytes(std::uint64_t byteCount) noexcept {
    rxByteCount_ += byteCount;
}

void NativeStatusCountersState::reset() noexcept {
    txByteCount_ = 0;
    rxByteCount_ = 0;
}

std::wstring NativeStatusCountersState::txStatusText() const {
    return L"TX " + std::to_wstring(txByteCount_) + L" B";
}

std::wstring NativeStatusCountersState::rxStatusText() const {
    return L"RX " + std::to_wstring(rxByteCount_) + L" B";
}

} // namespace svm::win32
