#include "win32/native_control_utils.h"

#if defined(_WIN32)

#include <algorithm>
#include <commctrl.h>
#include <cstdlib>
#include <cwctype>

namespace svm::win32 {
namespace {

bool controlGeometryMatches(HWND control, int x, int y, int width, int height) {
    RECT rect = {};
    if (GetWindowRect(control, &rect) == FALSE) {
        return false;
    }
    POINT topLeft = {rect.left, rect.top};
    HWND parent = GetParent(control);
    if (parent == nullptr || ScreenToClient(parent, &topLeft) == FALSE) {
        return false;
    }

    const int currentWidth = rect.right - rect.left;
    const int currentHeight = rect.bottom - rect.top;
    return topLeft.x == x && topLeft.y == y && currentWidth == width && currentHeight == height;
}

} // namespace

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
    const auto style = static_cast<LONG_PTR>(GetWindowLongPtrW(control, GWL_STYLE));
    const bool currentlyVisible = (style & WS_VISIBLE) != 0;
    if (currentlyVisible != visible) {
        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    }
}

void showControlFast(HWND control, bool visible) {
    showControl(control, visible);
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

void moveControl(HWND control, int x, int y, int width, int height, BOOL repaint) {
    if (control == nullptr) {
        return;
    }

    MoveWindow(control, x, y, width, height, repaint);
}

void moveTopControl(HWND control, int x, int y, int width, int height) {
    if (control == nullptr) {
        return;
    }

    SetWindowPos(control, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(control, nullptr, TRUE);
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

void addComboItem(HWND combo, const wchar_t* text, LPARAM data) {
    const LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    if (index >= 0) {
        SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), data);
    }
}

LPARAM selectedComboData(HWND combo, LPARAM fallback) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0) {
        return fallback;
    }
    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    return data == CB_ERR ? fallback : data;
}

void selectComboData(HWND combo, LPARAM data) {
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        if (SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) == data) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
            return;
        }
    }
    if (count > 0) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

int textToInt(HWND control, int fallback) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return fallback;
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    wchar_t* end = nullptr;
    const long value = std::wcstol(text.c_str(), &end, 10);
    return end != text.c_str() ? static_cast<int>(value) : fallback;
}

double textToDoubleText(const std::wstring& text, double fallback, bool* ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    if (text.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str()) {
        return fallback;
    }
    while (end != nullptr && *end != L'\0' && std::iswspace(*end)) {
        ++end;
    }
    if (end != nullptr && *end == L'\0') {
        if (ok != nullptr) {
            *ok = true;
        }
        return value;
    }
    return fallback;
}

double textToDouble(HWND control, double fallback, bool* ok) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        if (ok != nullptr) {
            *ok = false;
        }
        return fallback;
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return textToDoubleText(text, fallback, ok);
}

} // namespace svm::win32

#endif
