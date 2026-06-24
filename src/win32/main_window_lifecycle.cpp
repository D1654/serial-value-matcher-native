#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/ui_text.h"

namespace svm::win32 {
namespace {

using T = TextId;

constexpr wchar_t kWindowClassName[] = L"SvmNativeMainWindow";

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

bool NativeMainWindow::create(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = NativeMainWindow::windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&windowClass)) {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kWindowClassName,
        tx(T::WindowTitle),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1220,
        780,
        nullptr,
        nullptr,
        instance_,
        this);
    return window_ != nullptr;
}

void NativeMainWindow::show(int commandShow) {
    ShowWindow(window_, commandShow);
    UpdateWindow(window_);
}

int NativeMainWindow::runMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK NativeMainWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<NativeMainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<NativeMainWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    if (self != nullptr) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace svm::win32

#endif
