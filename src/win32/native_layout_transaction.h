#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace svm::win32 {

struct NativeLayoutTransactionStats {
    int requestedMoves = 0;
    int appliedMoves = 0;
    int skippedMoves = 0;
    int failedMoves = 0;
    int requestedVisibility = 0;
    int appliedVisibility = 0;
    int skippedVisibility = 0;
    int failedVisibility = 0;
    bool usedDeferredPositioning = false;
    bool deferredPositioningFailed = false;
};

class NativeLayoutTransaction final {
public:
    explicit NativeLayoutTransaction(BOOL repaint);

    void move(HWND control, int x, int y, int width, int height);
    void moveTop(HWND control, int x, int y, int width, int height);
    void show(HWND control, bool visible);
    void showFast(HWND control, bool visible);

    NativeLayoutTransactionStats commit();
    const NativeLayoutTransactionStats& stats() const noexcept;

private:
    struct MoveOperation {
        HWND control = nullptr;
        HWND insertAfter = nullptr;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        UINT flags = 0;
        bool invalidateAfterCommit = false;
    };

    struct VisibilityOperation {
        HWND control = nullptr;
        bool visible = false;
        bool fast = false;
    };

    bool applyMoveDirect(const MoveOperation& operation);
    void invalidateAfterMove(const MoveOperation& operation);
    void commitMoveOperations();
    void commitVisibilityOperations();

    BOOL repaint_ = TRUE;
    NativeLayoutTransactionStats stats_;
    std::vector<MoveOperation> moves_;
    std::vector<VisibilityOperation> visibility_;
};

} // namespace svm::win32

#endif
