#include "win32/main_window.h"

#if defined(_WIN32)

#include <cstddef>
#include <cstdio>
#include <string>

namespace svm::win32 {

void NativeMainWindow::setStatus(const std::wstring& text) {
    if (statusText_ != nullptr && cachedStatusText_ != text) {
        SetWindowTextW(statusText_, text.c_str());
        cachedStatusText_ = text;
    }
    updateStatusSegments();
}

void NativeMainWindow::updateStatusSegments() {
    if (txStatusText_ != nullptr) {
        const std::wstring txText = statusCountersState_.txStatusText();
        if (cachedTxStatusText_ != txText) {
            SetWindowTextW(txStatusText_, txText.c_str());
            cachedTxStatusText_ = txText;
        }
    }
    if (rxStatusText_ != nullptr) {
        const std::wstring rxText = statusCountersState_.rxStatusText();
        if (cachedRxStatusText_ != rxText) {
            SetWindowTextW(rxStatusText_, rxText.c_str());
            cachedRxStatusText_ = rxText;
        }
    }
    if (clockStatusText_ != nullptr) {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        wchar_t buffer[16] = {};
        swprintf_s(
            buffer,
            L"%02u:%02u:%02u",
            static_cast<unsigned int>(now.wHour),
            static_cast<unsigned int>(now.wMinute),
            static_cast<unsigned int>(now.wSecond));
        const std::wstring clockText(buffer);
        if (cachedClockStatusText_ != clockText) {
            SetWindowTextW(clockStatusText_, clockText.c_str());
            cachedClockStatusText_ = clockText;
        }
    }
}

std::wstring NativeMainWindow::controlText(HWND control) const {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

std::wstring NativeMainWindow::analysisInputText(HWND control) const {
    return controlText(control);
}

void NativeMainWindow::setControlText(HWND control, const std::wstring& text) {
    SetWindowTextW(control, text.c_str());
}

} // namespace svm::win32

#endif
