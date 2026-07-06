#pragma once

#include <cstddef>

namespace svm::win32 {

struct NativeLogFollowDecision {
    bool shouldFollow = true;
    bool shouldShowHistoryNotice = false;
};

class NativeLogScrollState final {
public:
    bool paused() const noexcept;
    bool autoFollow() const noexcept;
    bool historyReadNoticeShown() const noexcept;
    std::size_t hiddenLineCount() const noexcept;

    bool togglePause() noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void reset() noexcept;
    void clearContent() noexcept;
    void followLatest() noexcept;
    void markHistoryRead() noexcept;
    void noteAtBottom() noexcept;
    std::size_t noteHiddenLine() noexcept;
    bool shouldFollowAfterViewportChange(bool wasAtBottom) const noexcept;

    NativeLogFollowDecision followDecision(bool atBottom) noexcept;

private:
    bool paused_ = false;
    bool autoFollow_ = true;
    bool historyReadNoticeShown_ = false;
    std::size_t hiddenLineCount_ = 0;
};

} // namespace svm::win32
