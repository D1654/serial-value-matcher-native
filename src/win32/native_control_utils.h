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

bool nativeControlHasClass(HWND control, const wchar_t* expectedClassName);
void showControl(HWND control, bool visible);
void showControlFast(HWND control, bool visible);
void enableControl(HWND control, bool enabled);
void moveTopControl(HWND control, int x, int y, int width, int height);
void addControlFont(HWND control, HFONT font);
void applyClassicControlChrome(HWND control);
int singleLineEditHeight(HFONT font, int row);

} // namespace svm::win32

#endif
