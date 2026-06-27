#include "win32/native_progress_control.h"

#if defined(_WIN32)

#include <algorithm>
#include <commctrl.h>

namespace svm::win32 {
namespace {

constexpr wchar_t kNativeProgressClassName[] = L"SvmNativeProgress";
constexpr COLORREF kNativeProgressBorderColor = RGB(96, 96, 96);
constexpr COLORREF kNativeProgressBackgroundColor = RGB(255, 255, 255);
constexpr COLORREF kNativeProgressFillColor = RGB(0, 120, 215);

struct NativeProgressState {
    int minimum = 0;
    int maximum = 1000;
    int position = 0;
};

HBRUSH nativeProgressBorderBrush() {
    static HBRUSH brush = CreateSolidBrush(kNativeProgressBorderColor);
    return brush;
}

HBRUSH nativeProgressBackgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(kNativeProgressBackgroundColor);
    return brush;
}

HBRUSH nativeProgressFillBrush() {
    static HBRUSH brush = CreateSolidBrush(kNativeProgressFillColor);
    return brush;
}

int clampNativeProgressPosition(const NativeProgressState& state, int position) {
    return std::clamp(position, state.minimum, state.maximum);
}

void paintNativeProgress(HWND window, HDC dc) {
    RECT rect = {};
    GetClientRect(window, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return;
    }

    auto* state = reinterpret_cast<NativeProgressState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    NativeProgressState fallbackState;
    if (state == nullptr) {
        state = &fallbackState;
    }

    FillRect(dc, &rect, nativeProgressBorderBrush());

    RECT innerRect = {
        rect.left + 2,
        rect.top + 2,
        std::max<LONG>(rect.left + 2, rect.right - 2),
        std::max<LONG>(rect.top + 2, rect.bottom - 2),
    };
    FillRect(dc, &innerRect, nativeProgressBackgroundBrush());

    const int innerWidth = std::max(0, static_cast<int>(innerRect.right - innerRect.left));
    const int range = std::max(1, state->maximum - state->minimum);
    const int fillWidth = std::clamp(
        ((clampNativeProgressPosition(*state, state->position) - state->minimum) * innerWidth) / range,
        0,
        innerWidth);
    if (fillWidth > 0) {
        RECT fillRect = {innerRect.left, innerRect.top, innerRect.left + fillWidth, innerRect.bottom};
        FillRect(dc, &fillRect, nativeProgressFillBrush());
    }
}

LRESULT CALLBACK nativeProgressWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<NativeProgressState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE:
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new NativeProgressState()));
        return TRUE;
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        paintNativeProgress(window, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case PBM_SETRANGE32:
        if (state != nullptr) {
            const int oldMinimum = state->minimum;
            const int oldMaximum = state->maximum;
            const int oldPosition = state->position;
            state->minimum = static_cast<int>(wParam);
            state->maximum = static_cast<int>(lParam);
            if (state->maximum <= state->minimum) {
                state->maximum = state->minimum + 1;
            }
            state->position = clampNativeProgressPosition(*state, state->position);
            if (state->minimum != oldMinimum || state->maximum != oldMaximum || state->position != oldPosition) {
                InvalidateRect(window, nullptr, TRUE);
            }
        }
        return 0;
    case PBM_SETPOS:
        if (state != nullptr) {
            const int previousPosition = state->position;
            state->position = clampNativeProgressPosition(*state, static_cast<int>(wParam));
            if (state->position != previousPosition) {
                InvalidateRect(window, nullptr, TRUE);
            }
            return previousPosition;
        }
        return 0;
    case PBM_GETPOS:
        return state != nullptr ? state->position : 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool registerNativeProgressClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = nativeProgressWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kNativeProgressClassName;
    if (RegisterClassExW(&windowClass) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

HWND createNativeProgressControl(HWND parent, HINSTANCE instance, int controlId) {
    if (!registerNativeProgressClass(instance)) {
        return nullptr;
    }
    return CreateWindowExW(
        0,
        kNativeProgressClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0,
        0,
        0,
        0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        instance,
        nullptr);
}

bool nativeProgressStyleHasVisibleFrame(COLORREF formBackgroundColor) {
    return kNativeProgressBorderColor != formBackgroundColor
        && kNativeProgressBorderColor != kNativeProgressBackgroundColor
        && kNativeProgressClassName[0] != L'\0';
}

} // namespace svm::win32

#endif
