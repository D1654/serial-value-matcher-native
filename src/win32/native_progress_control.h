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

HWND createNativeProgressControl(HWND parent, HINSTANCE instance, int controlId);
bool nativeProgressStyleHasVisibleFrame(COLORREF formBackgroundColor);

} // namespace svm::win32

#endif
