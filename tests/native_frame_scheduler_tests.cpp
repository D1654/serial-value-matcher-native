#include "win32/native_frame_scheduler.h"

#include <cstdio>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        return false;
    }
    return true;
}

bool testResizeCoalescesToLatestClientSize() {
    svm::win32::NativeFrameScheduler scheduler;
    const bool firstPost = scheduler.requestResize(800, 600);
    const bool secondPost = scheduler.requestResize(1024, 768);
    const svm::win32::NativeFrameSnapshot frame = scheduler.consumeFrame();
    return expect(firstPost, "first resize should request a post")
        && expect(!secondPost, "second resize should coalesce")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::Resize), "resize reason should be present")
        && expect(frame.hasClientSize, "resize should carry client size")
        && expect(frame.clientWidth == 1024 && frame.clientHeight == 768, "resize should preserve latest client size")
        && expect(scheduler.stats().postRequests == 1, "coalesced resize should post once")
        && expect(scheduler.stats().coalescedRequests == 1, "coalesced resize should count one coalesced request");
}

bool testSplitterDragPreservesLatestTarget() {
    svm::win32::NativeFrameScheduler scheduler;
    const bool firstPost = scheduler.requestSplitterDrag(900, 640, 180);
    const bool secondPost = scheduler.requestSplitterDrag(900, 640, 187);
    const bool thirdPost = scheduler.requestSplitterDrag(900, 640, 193);
    const svm::win32::NativeFrameSnapshot frame = scheduler.consumeFrame();
    return expect(firstPost, "first splitter drag should request a post")
        && expect(!secondPost && !thirdPost, "subsequent splitter drags should coalesce")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::SplitterDrag), "splitter reason should be present")
        && expect(frame.hasWorkbenchHeight, "splitter drag should carry workbench height")
        && expect(frame.workbenchHeight == 193, "splitter drag should preserve latest exact height")
        && expect(frame.hasClientSize && frame.clientWidth == 900 && frame.clientHeight == 640, "splitter drag should preserve latest client size");
}

bool testMultipleReasonsShareOnePostedFrame() {
    svm::win32::NativeFrameScheduler scheduler;
    const bool resizePost = scheduler.requestResize(760, 520);
    const bool tabPost = scheduler.requestTabSwitch();
    const bool logPost = scheduler.requestLogFlush();
    const bool statusPost = scheduler.requestStatus();
    const svm::win32::NativeFrameSnapshot frame = scheduler.consumeFrame();
    return expect(resizePost, "first mixed reason should request a post")
        && expect(!tabPost && !logPost && !statusPost, "later mixed reasons should coalesce")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::Resize), "mixed frame should include resize")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::TabSwitch), "mixed frame should include tab switch")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::LogFlush), "mixed frame should include log flush")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::Status), "mixed frame should include status")
        && expect(scheduler.stats().postRequests == 1, "mixed frame should post once")
        && expect(scheduler.stats().consumedFrames == 1, "mixed frame should consume once");
}

bool testPostFailureKeepsPendingWork() {
    svm::win32::NativeFrameScheduler scheduler;
    const bool shouldPost = scheduler.requestResize(640, 480);
    scheduler.markPostFailed();
    const bool retryPost = scheduler.requestStatus();
    const svm::win32::NativeFrameSnapshot frame = scheduler.consumeFrame();
    return expect(shouldPost, "initial request should need a post")
        && expect(retryPost, "post failure should allow retry post")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::Resize), "failed-post frame should keep resize")
        && expect(nativeFrameHasReason(frame.reasons, svm::win32::NativeFrameReason::Status), "failed-post frame should add retry reason")
        && expect(frame.hasClientSize && frame.clientWidth == 640 && frame.clientHeight == 480, "failed-post frame should keep client size")
        && expect(scheduler.stats().postFailures == 1, "post failure should be counted");
}

} // namespace

int main() {
    if (!testResizeCoalescesToLatestClientSize()
        || !testSplitterDragPreservesLatestTarget()
        || !testMultipleReasonsShareOnePostedFrame()
        || !testPostFailureKeepsPendingWork()) {
        return 1;
    }

    std::puts("native_frame_scheduler_tests passed");
    return 0;
}
