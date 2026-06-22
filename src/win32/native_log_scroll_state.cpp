#include "win32/native_log_scroll_state.h"

namespace svm::win32 {

bool NativeLogScrollState::paused() const noexcept {
    return paused_;
}

bool NativeLogScrollState::autoFollow() const noexcept {
    return autoFollow_;
}

bool NativeLogScrollState::historyReadNoticeShown() const noexcept {
    return historyReadNoticeShown_;
}

std::size_t NativeLogScrollState::hiddenLineCount() const noexcept {
    return hiddenLineCount_;
}

bool NativeLogScrollState::togglePause() noexcept {
    paused_ = !paused_;
    if (!paused_) {
        hiddenLineCount_ = 0;
        followLatest();
    }
    return paused_;
}

void NativeLogScrollState::pause() noexcept {
    paused_ = true;
}

void NativeLogScrollState::resume() noexcept {
    paused_ = false;
    hiddenLineCount_ = 0;
}

void NativeLogScrollState::reset() noexcept {
    paused_ = false;
    hiddenLineCount_ = 0;
    followLatest();
}

void NativeLogScrollState::clearContent() noexcept {
    hiddenLineCount_ = 0;
    followLatest();
}

void NativeLogScrollState::followLatest() noexcept {
    autoFollow_ = true;
    historyReadNoticeShown_ = false;
}

void NativeLogScrollState::markHistoryRead() noexcept {
    autoFollow_ = false;
    historyReadNoticeShown_ = true;
}

void NativeLogScrollState::noteAtBottom() noexcept {
    followLatest();
}

std::size_t NativeLogScrollState::noteHiddenLine() noexcept {
    return ++hiddenLineCount_;
}

NativeLogFollowDecision NativeLogScrollState::followDecision(bool atBottom) noexcept {
    if (atBottom) {
        noteAtBottom();
    }
    const bool shouldFollow = autoFollow_ && atBottom;
    if (shouldFollow) {
        return {true, false};
    }
    autoFollow_ = false;
    const bool shouldShowNotice = !historyReadNoticeShown_;
    historyReadNoticeShown_ = true;
    return {false, shouldShowNotice};
}

} // namespace svm::win32
