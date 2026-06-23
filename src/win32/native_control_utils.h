#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace svm::win32 {

bool nativeControlHasClass(HWND control, const wchar_t* expectedClassName);
void showControl(HWND control, bool visible);
void showControlFast(HWND control, bool visible);
void enableControl(HWND control, bool enabled);
void moveTopControl(HWND control, int x, int y, int width, int height);
void addControlFont(HWND control, HFONT font);
void applyClassicControlChrome(HWND control);
int singleLineEditHeight(HFONT font, int row);
void addComboItem(HWND combo, const wchar_t* text, LPARAM data);
LPARAM selectedComboData(HWND combo, LPARAM fallback = 0);
void selectComboData(HWND combo, LPARAM data);
int textToInt(HWND control, int fallback);
double textToDoubleText(const std::wstring& text, double fallback, bool* ok = nullptr);
double textToDouble(HWND control, double fallback, bool* ok = nullptr);

} // namespace svm::win32

#endif
