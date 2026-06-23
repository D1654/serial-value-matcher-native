#include "win32/native_control_utils.h"

#if defined(_WIN32)

#include <algorithm>
#include <commctrl.h>

namespace svm::win32 {

bool nativeControlHasClass(HWND control, const wchar_t* expectedClassName) {
    wchar_t className[32] = {};
    if (control == nullptr || GetClassNameW(control, className, static_cast<int>(sizeof(className) / sizeof(className[0]))) == 0) {
        return false;
    }
    return lstrcmpiW(className, expectedClassName) == 0;
}

void showControl(HWND control, bool visible) {
    if (control == nullptr) {
        return;
    }
    const bool currentlyVisible = IsWindowVisible(control) != FALSE;
    if (currentlyVisible != visible) {
        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
}

void showControlFast(HWND control, bool visible) {
    if (control == nullptr) {
        return;
    }
    const auto style = static_cast<LONG_PTR>(GetWindowLongPtrW(control, GWL_STYLE));
    const bool currentlyVisible = (style & WS_VISIBLE) != 0;
    if (currentlyVisible == visible) {
        return;
    }
    const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
    SetWindowPos(control, nullptr, 0, 0, 0, 0, flags);
}

void enableControl(HWND control, bool enabled) {
    if (control == nullptr) {
        return;
    }
    const bool currentlyEnabled = IsWindowEnabled(control) != FALSE;
    if (currentlyEnabled != enabled) {
        EnableWindow(control, enabled ? TRUE : FALSE);
    }
}

void moveTopControl(HWND control, int x, int y, int width, int height) {
    if (control != nullptr) {
        SetWindowPos(control, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(control, nullptr, TRUE);
    }
}

void addControlFont(HWND control, HFONT font) {
    if (control != nullptr && font != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void applyClassicControlChrome(HWND control) {
    if (control == nullptr) {
        return;
    }
    if (nativeControlHasClass(control, L"ComboBox")) {
        SendMessageW(control, CB_SETMINVISIBLE, 10, 0);
    }
}

int singleLineEditHeight(HFONT font, int row) {
    int textHeight = 12;
    HDC dc = GetDC(nullptr);
    if (dc != nullptr) {
        HGDIOBJ oldFont = nullptr;
        if (font != nullptr) {
            oldFont = SelectObject(dc, font);
        }
        TEXTMETRICW metrics = {};
        if (GetTextMetricsW(dc, &metrics) != FALSE) {
            textHeight = metrics.tmHeight;
        }
        if (oldFont != nullptr) {
            SelectObject(dc, oldFont);
        }
        ReleaseDC(nullptr, dc);
    }
    const int naturalHeight = textHeight + GetSystemMetrics(SM_CYBORDER) * 2 + 4;
    return std::clamp(naturalHeight, std::max(14, row - 5), row);
}

} // namespace svm::win32

#endif
