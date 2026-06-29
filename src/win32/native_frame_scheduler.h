#pragma once

namespace svm::win32 {

enum class NativeFrameReason : unsigned int {
    None = 0,
    Resize = 1u << 0,
    SplitterDrag = 1u << 1,
    TabSwitch = 1u << 2,
    LogFlush = 1u << 3,
    Status = 1u << 4,
    Progress = 1u << 5,
    Settle = 1u << 6,
};

constexpr NativeFrameReason operator|(NativeFrameReason left, NativeFrameReason right) noexcept {
    return static_cast<NativeFrameReason>(
        static_cast<unsigned int>(left) | static_cast<unsigned int>(right));
}

constexpr NativeFrameReason& operator|=(NativeFrameReason& left, NativeFrameReason right) noexcept {
    left = left | right;
    return left;
}

constexpr bool nativeFrameHasReason(NativeFrameReason value, NativeFrameReason reason) noexcept {
    return (static_cast<unsigned int>(value) & static_cast<unsigned int>(reason)) != 0;
}

struct NativeFrameSchedulerStats {
    int requestedFrames = 0;
    int postRequests = 0;
    int coalescedRequests = 0;
    int consumedFrames = 0;
    int postFailures = 0;
};

struct NativeFrameSnapshot {
    NativeFrameReason reasons = NativeFrameReason::None;
    bool hasClientSize = false;
    int clientWidth = 0;
    int clientHeight = 0;
    bool hasWorkbenchHeight = false;
    int workbenchHeight = 0;

    bool empty() const noexcept {
        return reasons == NativeFrameReason::None;
    }
};

class NativeFrameScheduler final {
public:
    bool requestResize(int clientWidth, int clientHeight);
    bool requestSplitterDrag(int clientWidth, int clientHeight, int workbenchHeight);
    bool requestTabSwitch();
    bool requestLogFlush();
    bool requestStatus();
    bool requestProgress();
    bool requestSettle();

    NativeFrameSnapshot consumeFrame();
    void markPostFailed();

    bool frameQueued() const noexcept;
    NativeFrameReason pendingReasons() const noexcept;
    const NativeFrameSchedulerStats& stats() const noexcept;

private:
    bool requestFrame(NativeFrameReason reason);
    void rememberClientSize(int clientWidth, int clientHeight) noexcept;

    NativeFrameSnapshot pending_;
    NativeFrameSchedulerStats stats_;
    bool frameQueued_ = false;
};

} // namespace svm::win32
