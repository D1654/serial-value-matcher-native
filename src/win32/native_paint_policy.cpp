#include "win32/native_paint_policy.h"

#if defined(_WIN32)

namespace svm::win32 {

UINT nativeLiveRegionRedrawFlags() noexcept {
    return RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE;
}

UINT nativeFullRefreshRedrawFlags() noexcept {
    return RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW;
}

UINT nativeLogFlushRedrawFlags() noexcept {
    return RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN;
}

UINT nativeWorkbenchTabRedrawFlags(bool dragging) noexcept {
    return dragging
        ? (RDW_INVALIDATE | RDW_NOERASE)
        : (RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

UINT nativeWorkbenchAreaRedrawFlags(bool dragging) noexcept {
    return dragging ? nativeLiveRegionRedrawFlags() : nativeFullRefreshRedrawFlags();
}

UINT nativeWorkbenchBackgroundPositionFlags(bool dragging) noexcept {
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (dragging) {
        flags |= SWP_NOREDRAW;
    }
    return flags;
}

UINT nativeRaiseNoRedrawFlags() noexcept {
    return SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW;
}

bool nativeRedrawFlagsForceImmediatePaint(UINT flags) noexcept {
    return (flags & RDW_UPDATENOW) != 0;
}

bool nativeRedrawFlagsEraseBackground(UINT flags) noexcept {
    return (flags & RDW_ERASE) != 0 && (flags & RDW_NOERASE) == 0;
}

void nativeSetWindowRedraw(HWND window, bool enabled) {
    if (window != nullptr) {
        SendMessageW(window, WM_SETREDRAW, enabled ? TRUE : FALSE, 0);
    }
}

void nativeRedrawLiveRegion(HWND window, const RECT* rect) {
    if (window != nullptr) {
        RedrawWindow(window, rect, nullptr, nativeLiveRegionRedrawFlags());
    }
}

void nativeRedrawFullRefresh(HWND window) {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, nativeFullRefreshRedrawFlags());
    }
}

void nativeRedrawFirstShow(HWND window) {
    nativeRedrawFullRefresh(window);
    if (window != nullptr) {
        UpdateWindow(window);
    }
}

void nativeRedrawLogFlush(HWND window) {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, nativeLogFlushRedrawFlags());
    }
}

void nativeRedrawWorkbenchTab(HWND window, bool dragging) {
    if (window != nullptr) {
        RedrawWindow(window, nullptr, nullptr, nativeWorkbenchTabRedrawFlags(dragging));
    }
}

void nativeRedrawWorkbenchArea(HWND window, const RECT* rect, bool dragging) {
    if (window != nullptr) {
        RedrawWindow(window, rect, nullptr, nativeWorkbenchAreaRedrawFlags(dragging));
    }
}

void nativeInvalidateControl(HWND control, bool erase) {
    if (control != nullptr) {
        InvalidateRect(control, nullptr, erase ? TRUE : FALSE);
    }
}

void nativeRaiseWindowNoRedraw(HWND control, HWND insertAfter) {
    if (control != nullptr) {
        SetWindowPos(control, insertAfter, 0, 0, 0, 0, nativeRaiseNoRedrawFlags());
    }
}

} // namespace svm::win32

#endif
