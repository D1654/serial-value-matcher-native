#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace svm::win32 {

UINT nativeLiveRegionRedrawFlags() noexcept;
UINT nativeResizeRedrawFlags(bool liveResize) noexcept;
UINT nativeFullRefreshRedrawFlags() noexcept;
UINT nativeLogFlushRedrawFlags() noexcept;
UINT nativeWorkbenchTabRedrawFlags(bool dragging) noexcept;
UINT nativeWorkbenchAreaRedrawFlags(bool dragging) noexcept;
UINT nativeWorkbenchBackgroundPositionFlags(bool dragging) noexcept;
UINT nativeRaiseNoRedrawFlags() noexcept;
bool nativeRedrawFlagsForceImmediatePaint(UINT flags) noexcept;
bool nativeRedrawFlagsEraseBackground(UINT flags) noexcept;
bool nativeShouldSuppressBackgroundErase(bool liveResize, bool draggingSplitter) noexcept;

void nativeSetWindowRedraw(HWND window, bool enabled);
void nativeRedrawLiveRegion(HWND window, const RECT* rect);
void nativeRedrawResize(HWND window, bool liveResize);
void nativeRedrawFullRefresh(HWND window);
void nativeRedrawFirstShow(HWND window);
void nativeRedrawLogFlush(HWND window);
void nativeRedrawWorkbenchTab(HWND window, bool dragging);
void nativeRedrawWorkbenchArea(HWND window, const RECT* rect, bool dragging);
void nativeInvalidateControl(HWND control, bool erase);
void nativeRaiseWindowNoRedraw(HWND control, HWND insertAfter);

} // namespace svm::win32

#endif
