#include "win32/native_frame_scheduler.h"

namespace svm::win32 {

bool NativeFrameScheduler::requestResize(int clientWidth, int clientHeight) {
    rememberClientSize(clientWidth, clientHeight);
    return requestFrame(NativeFrameReason::Resize);
}

bool NativeFrameScheduler::requestSplitterDrag(int clientWidth, int clientHeight, int workbenchHeight) {
    rememberClientSize(clientWidth, clientHeight);
    pending_.hasWorkbenchHeight = true;
    pending_.workbenchHeight = workbenchHeight;
    return requestFrame(NativeFrameReason::SplitterDrag);
}

bool NativeFrameScheduler::requestTabSwitch() {
    return requestFrame(NativeFrameReason::TabSwitch);
}

bool NativeFrameScheduler::requestLogFlush() {
    return requestFrame(NativeFrameReason::LogFlush);
}

bool NativeFrameScheduler::requestStatus() {
    return requestFrame(NativeFrameReason::Status);
}

bool NativeFrameScheduler::requestProgress() {
    return requestFrame(NativeFrameReason::Progress);
}

bool NativeFrameScheduler::requestSettle() {
    return requestFrame(NativeFrameReason::Settle);
}

NativeFrameSnapshot NativeFrameScheduler::consumeFrame() {
    NativeFrameSnapshot snapshot = pending_;
    pending_ = {};
    frameQueued_ = false;
    if (!snapshot.empty()) {
        ++stats_.consumedFrames;
    }
    return snapshot;
}

void NativeFrameScheduler::markPostFailed() {
    if (frameQueued_) {
        frameQueued_ = false;
        ++stats_.postFailures;
    }
}

bool NativeFrameScheduler::frameQueued() const noexcept {
    return frameQueued_;
}

NativeFrameReason NativeFrameScheduler::pendingReasons() const noexcept {
    return pending_.reasons;
}

const NativeFrameSchedulerStats& NativeFrameScheduler::stats() const noexcept {
    return stats_;
}

bool NativeFrameScheduler::requestFrame(NativeFrameReason reason) {
    pending_.reasons |= reason;
    ++stats_.requestedFrames;
    if (frameQueued_) {
        ++stats_.coalescedRequests;
        return false;
    }
    frameQueued_ = true;
    ++stats_.postRequests;
    return true;
}

void NativeFrameScheduler::rememberClientSize(int clientWidth, int clientHeight) noexcept {
    pending_.hasClientSize = true;
    pending_.clientWidth = clientWidth;
    pending_.clientHeight = clientHeight;
}

} // namespace svm::win32
