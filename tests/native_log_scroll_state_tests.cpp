#include "win32/native_log_scroll_state.h"

#include <cassert>
#include <iostream>

namespace {

void pauseCountsHiddenLinesAndResumeClearsCount() {
    svm::win32::NativeLogScrollState state;
    assert(!state.paused());

    assert(state.togglePause());
    assert(state.paused());
    assert(state.noteHiddenLine() == 1);
    assert(state.noteHiddenLine() == 2);
    assert(state.hiddenLineCount() == 2);

    assert(!state.togglePause());
    assert(!state.paused());
    assert(state.hiddenLineCount() == 0);
    assert(state.autoFollow());
}

void followDecisionTracksUserHistoryReading() {
    svm::win32::NativeLogScrollState state;
    auto decision = state.followDecision(true);
    assert(decision.shouldFollow);
    assert(!decision.shouldShowHistoryNotice);

    decision = state.followDecision(false);
    assert(!decision.shouldFollow);
    assert(decision.shouldShowHistoryNotice);
    assert(state.historyReadNoticeShown());
    assert(!state.autoFollow());

    decision = state.followDecision(false);
    assert(!decision.shouldFollow);
    assert(!decision.shouldShowHistoryNotice);

    decision = state.followDecision(true);
    assert(decision.shouldFollow);
    assert(!decision.shouldShowHistoryNotice);
    assert(state.autoFollow());
    assert(!state.historyReadNoticeShown());
}

void explicitFollowAndResetRestoreStreamingMode() {
    svm::win32::NativeLogScrollState state;
    state.pause();
    state.noteHiddenLine();
    state.markHistoryRead();
    assert(state.paused());
    assert(state.hiddenLineCount() == 1);
    assert(!state.autoFollow());

    state.followLatest();
    assert(state.autoFollow());
    assert(!state.historyReadNoticeShown());
    assert(state.paused());

    state.reset();
    assert(!state.paused());
    assert(state.hiddenLineCount() == 0);
    assert(state.autoFollow());
}

void clearContentKeepsPauseButResetsHiddenCount() {
    svm::win32::NativeLogScrollState state;
    state.pause();
    state.noteHiddenLine();
    state.markHistoryRead();

    state.clearContent();
    assert(state.paused());
    assert(state.hiddenLineCount() == 0);
    assert(state.autoFollow());
    assert(!state.historyReadNoticeShown());
}

} // namespace

int main() {
    pauseCountsHiddenLinesAndResumeClearsCount();
    followDecisionTracksUserHistoryReading();
    explicitFollowAndResetRestoreStreamingMode();
    clearContentKeepsPauseButResetsHiddenCount();

    std::cout << "native_log_scroll_state_tests passed\n";
    return 0;
}
