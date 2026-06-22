#include "win32/native_log_view.h"

#if defined(_WIN32)

#include <algorithm>
#include <richedit.h>

namespace svm::win32 {
namespace {

struct NativeLogPalette {
    COLORREF background = RGB(255, 255, 255);
    COLORREF normal = RGB(32, 32, 32);
    COLORREF system = RGB(84, 84, 84);
    COLORREF tx = RGB(0, 91, 170);
    COLORREF rx = RGB(0, 128, 72);
    COLORREF modbusTx = RGB(138, 82, 0);
    COLORREF modbusRx = RGB(108, 72, 145);
    COLORREF error = RGB(176, 38, 38);
};

NativeLogPalette logPalette(int themeIndex) {
    switch (themeIndex) {
    case 1:
        return {
            RGB(250, 250, 248),
            RGB(32, 32, 32),
            RGB(88, 88, 88),
            RGB(35, 95, 154),
            RGB(35, 120, 84),
            RGB(130, 86, 36),
            RGB(106, 84, 150),
            RGB(170, 48, 48),
        };
    case 2:
        return {
            RGB(12, 12, 12),
            RGB(232, 232, 232),
            RGB(210, 210, 210),
            RGB(70, 190, 255),
            RGB(95, 230, 130),
            RGB(255, 210, 72),
            RGB(215, 145, 255),
            RGB(255, 88, 88),
        };
    default:
        return {};
    }
}

COLORREF logColorForKind(NativeLogKind kind, int themeIndex) {
    const NativeLogPalette palette = logPalette(themeIndex);
    switch (kind) {
    case NativeLogKind::System:
        return palette.system;
    case NativeLogKind::Tx:
        return palette.tx;
    case NativeLogKind::Rx:
        return palette.rx;
    case NativeLogKind::ModbusTx:
        return palette.modbusTx;
    case NativeLogKind::ModbusRx:
        return palette.modbusRx;
    case NativeLogKind::Error:
        return palette.error;
    }
    return palette.normal;
}

} // namespace

NativeLogRedrawGuard::NativeLogRedrawGuard(HWND window)
    : window_(window) {
    if (window_ != nullptr) {
        SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }
}

NativeLogRedrawGuard::~NativeLogRedrawGuard() {
    if (window_ != nullptr) {
        SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
    }
}

bool nativeLogIsAtBottom(HWND logControl) {
    if (logControl == nullptr) {
        return true;
    }

    SCROLLINFO scrollInfo = {};
    scrollInfo.cbSize = sizeof(scrollInfo);
    scrollInfo.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
    if (!GetScrollInfo(logControl, SB_VERT, &scrollInfo)) {
        return true;
    }

    const int page = scrollInfo.nPage > 0 ? static_cast<int>(scrollInfo.nPage) : 1;
    return scrollInfo.nPos + page >= scrollInfo.nMax - 1;
}

int nativeLogFirstVisibleLine(HWND logControl) {
    if (logControl == nullptr) {
        return 0;
    }
    const LRESULT line = SendMessageW(logControl, EM_GETFIRSTVISIBLELINE, 0, 0);
    return line < 0 ? 0 : static_cast<int>(line);
}

void nativeLogRestoreFirstVisibleLine(HWND logControl, int firstVisibleLine) {
    if (logControl == nullptr) {
        return;
    }
    const int currentFirstLine = nativeLogFirstVisibleLine(logControl);
    const int delta = firstVisibleLine - currentFirstLine;
    if (delta != 0) {
        SendMessageW(logControl, EM_LINESCROLL, 0, delta);
    }
}

void nativeLogScrollToBottom(HWND logControl) {
    if (logControl == nullptr) {
        return;
    }
    SendMessageW(logControl, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    SendMessageW(logControl, EM_SCROLLCARET, 0, 0);
    SendMessageW(logControl, WM_VSCROLL, SB_BOTTOM, 0);
}

void nativeLogScrollCaret(HWND logControl) {
    if (logControl != nullptr) {
        SendMessageW(logControl, EM_SCROLLCARET, 0, 0);
    }
}

NativeLogSelection nativeLogSelection(HWND logControl) {
    NativeLogSelection selection;
    if (logControl != nullptr) {
        SendMessageW(logControl, EM_GETSEL, reinterpret_cast<WPARAM>(&selection.start), reinterpret_cast<LPARAM>(&selection.end));
    }
    return selection;
}

void nativeLogSetSelection(HWND logControl, DWORD start, DWORD end) {
    if (logControl != nullptr) {
        SendMessageW(logControl, EM_SETSEL, start, end);
    }
}

void nativeLogSetSelection(HWND logControl, std::size_t start, std::size_t end) {
    if (logControl != nullptr) {
        SendMessageW(logControl, EM_SETSEL, static_cast<WPARAM>(start), static_cast<LPARAM>(end));
    }
}

void nativeLogInsertText(HWND logControl, bool usesRichEdit, int themeIndex, NativeLogKind kind, const std::wstring& text) {
    if (logControl == nullptr) {
        return;
    }
    SendMessageW(logControl, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    if (usesRichEdit) {
        CHARFORMAT2W format = {};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        format.crTextColor = logColorForKind(kind, themeIndex);
        SendMessageW(logControl, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    }
    SendMessageW(logControl, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void nativeLogApplyTheme(HWND logControl, bool usesRichEdit, int themeIndex) {
    if (logControl == nullptr || !usesRichEdit) {
        return;
    }

    const NativeLogPalette palette = logPalette(themeIndex);
    SendMessageW(logControl, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(palette.background));
    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = palette.normal;
    SendMessageW(logControl, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
    InvalidateRect(logControl, nullptr, TRUE);
}

void nativeLogSetTextLimit(HWND logControl, std::size_t limit) {
    if (logControl != nullptr) {
        SendMessageW(logControl, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(limit));
    }
}

} // namespace svm::win32

#endif
