#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "win32/native_log_model.h"

#include <cstddef>
#include <string>

namespace svm::win32 {

class NativeLogRedrawGuard {
public:
    explicit NativeLogRedrawGuard(HWND window);
    ~NativeLogRedrawGuard();

    NativeLogRedrawGuard(const NativeLogRedrawGuard&) = delete;
    NativeLogRedrawGuard& operator=(const NativeLogRedrawGuard&) = delete;

private:
    HWND window_ = nullptr;
};

struct NativeLogSelection {
    DWORD start = 0;
    DWORD end = 0;
};

bool nativeLogIsAtBottom(HWND logControl);
int nativeLogFirstVisibleLine(HWND logControl);
void nativeLogRestoreFirstVisibleLine(HWND logControl, int firstVisibleLine);
void nativeLogScrollToBottom(HWND logControl);
void nativeLogScrollCaret(HWND logControl);
NativeLogSelection nativeLogSelection(HWND logControl);
void nativeLogSetSelection(HWND logControl, DWORD start, DWORD end);
void nativeLogSetSelection(HWND logControl, std::size_t start, std::size_t end);
void nativeLogInsertText(HWND logControl, bool usesRichEdit, int themeIndex, NativeLogKind kind, const std::wstring& text);
void nativeLogApplyTheme(HWND logControl, bool usesRichEdit, int themeIndex);
void nativeLogSetTextLimit(HWND logControl, std::size_t limit);

} // namespace svm::win32

#endif
