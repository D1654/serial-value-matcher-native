#include "win32/native_layout_transaction.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"

namespace svm::win32 {

NativeLayoutTransaction::NativeLayoutTransaction(BOOL repaint)
    : repaint_(repaint) {
}

void NativeLayoutTransaction::move(HWND control, int x, int y, int width, int height) {
    if (control == nullptr || committed_) {
        return;
    }

    ++stats_.requestedMoves;
    if (nativeControlGeometryMatches(control, x, y, width, height)) {
        ++stats_.skippedMoves;
        return;
    }

    UINT flags = SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE;
    if (repaint_ == FALSE) {
        flags |= SWP_NOREDRAW;
    }
    moves_.push_back({control, nullptr, x, y, width, height, flags, false});
}

void NativeLayoutTransaction::moveTop(HWND control, int x, int y, int width, int height) {
    if (control == nullptr || committed_) {
        return;
    }

    ++stats_.requestedMoves;
    const bool geometryUnchanged = nativeControlGeometryMatches(control, x, y, width, height);
    UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
    if (repaint_ == FALSE) {
        flags |= SWP_NOREDRAW;
    }
    if (geometryUnchanged) {
        flags |= SWP_NOMOVE | SWP_NOSIZE;
    }
    moves_.push_back({
        control,
        HWND_TOP,
        x,
        y,
        width,
        height,
        flags,
        repaint_ != FALSE && !geometryUnchanged,
    });
}

void NativeLayoutTransaction::show(HWND control, bool visible) {
    if (control == nullptr || committed_) {
        return;
    }

    ++stats_.requestedVisibility;
    if (nativeControlIsVisible(control) == visible) {
        ++stats_.skippedVisibility;
        return;
    }
    visibility_.push_back({control, visible, false});
}

void NativeLayoutTransaction::showFast(HWND control, bool visible) {
    if (control == nullptr || committed_) {
        return;
    }

    ++stats_.requestedVisibility;
    if (nativeControlIsVisible(control) == visible) {
        ++stats_.skippedVisibility;
        return;
    }
    visibility_.push_back({control, visible, true});
}

NativeLayoutTransactionStats NativeLayoutTransaction::commit() {
    ++stats_.commitCalls;
    if (committed_) {
        ++stats_.duplicateCommits;
        return stats_;
    }
    committed_ = true;
    commitMoveOperations();
    commitVisibilityOperations();
    moves_.clear();
    visibility_.clear();
    return stats_;
}

const NativeLayoutTransactionStats& NativeLayoutTransaction::stats() const noexcept {
    return stats_;
}

bool NativeLayoutTransaction::applyMoveDirect(const MoveOperation& operation) {
    if (operation.insertAfter == nullptr
        && (operation.flags & (SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_HIDEWINDOW)) == 0
        && nativeControlGeometryMatches(operation.control, operation.x, operation.y, operation.width, operation.height)) {
        ++stats_.skippedMoves;
        return true;
    }

    if (SetWindowPos(
            operation.control,
            operation.insertAfter,
            operation.x,
            operation.y,
            operation.width,
            operation.height,
            operation.flags)
        == FALSE) {
        ++stats_.failedMoves;
        return false;
    }

    ++stats_.appliedMoves;
    invalidateAfterMove(operation);
    return true;
}

void NativeLayoutTransaction::invalidateAfterMove(const MoveOperation& operation) {
    if (operation.invalidateAfterCommit) {
        InvalidateRect(operation.control, nullptr, TRUE);
    }
}

void NativeLayoutTransaction::commitMoveOperations() {
    if (moves_.empty()) {
        return;
    }

    if (moves_.size() > 1) {
        HDWP deferred = BeginDeferWindowPos(static_cast<int>(moves_.size()));
        bool deferredOk = deferred != nullptr;
        if (deferredOk) {
            for (const MoveOperation& operation : moves_) {
                HDWP next = DeferWindowPos(
                    deferred,
                    operation.control,
                    operation.insertAfter,
                    operation.x,
                    operation.y,
                    operation.width,
                    operation.height,
                    operation.flags);
                if (next == nullptr) {
                    deferredOk = false;
                    break;
                }
                deferred = next;
            }
        }

        if (deferredOk && EndDeferWindowPos(deferred) != FALSE) {
            stats_.usedDeferredPositioning = true;
            stats_.appliedMoves += static_cast<int>(moves_.size());
            for (const MoveOperation& operation : moves_) {
                invalidateAfterMove(operation);
            }
            return;
        }

        stats_.deferredPositioningFailed = true;
    }

    for (const MoveOperation& operation : moves_) {
        applyMoveDirect(operation);
    }
}

void NativeLayoutTransaction::commitVisibilityOperations() {
    for (const VisibilityOperation& operation : visibility_) {
        bool succeeded = true;
        if (operation.fast) {
            const UINT visibilityFlag = operation.visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
            succeeded = SetWindowPos(
                operation.control,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | visibilityFlag)
                != FALSE;
        } else {
            ShowWindow(operation.control, operation.visible ? SW_SHOWNA : SW_HIDE);
            succeeded = nativeControlIsVisible(operation.control) == operation.visible;
        }

        if (succeeded) {
            ++stats_.appliedVisibility;
        } else {
            ++stats_.failedVisibility;
        }
    }
}

} // namespace svm::win32

#endif
