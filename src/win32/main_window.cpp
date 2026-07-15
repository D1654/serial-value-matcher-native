#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_layout_metrics.h"
#include "win32/native_paint_policy.h"

#include <algorithm>

namespace svm::win32 {
namespace {

int signedLowWord(LPARAM value) noexcept {
    return static_cast<int>(static_cast<short>(LOWORD(value)));
}

int signedHighWord(LPARAM value) noexcept {
    return static_cast<int>(static_cast<short>(HIWORD(value)));
}

} // namespace

LRESULT NativeMainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
        minMax->ptMinTrackSize.x = kMinTrackWidth;
        minMax->ptMinTrackSize.y = kMinTrackHeight;
        return 0;
    }
    case WM_CREATE:
        return handleCreateMessage();
    case WM_SIZE:
        scheduleResizeFrame(LOWORD(lParam), HIWORD(lParam));
        if (wParam != SIZE_MINIMIZED && !trackingWindowSizeMove_) {
            scheduleUiPreferencesSave();
        }
        return 0;
    case WM_ENTERSIZEMOVE:
        trackingWindowSizeMove_ = true;
        return 0;
    case WM_EXITSIZEMOVE:
        trackingWindowSizeMove_ = false;
        postNativeFrameMessage(frameScheduler_.requestSettle());
        saveUiPreferences();
        return 0;
    case WM_ERASEBKGND:
        if (nativeShouldSuppressBackgroundErase(trackingWindowSizeMove_, draggingWorkbenchSplitter_)) {
            return 1;
        }
        break;
    case WM_PAINT:
        paintLayoutChrome();
        return 0;
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point = {};
            GetCursorPos(&point);
            ScreenToClient(window_, &point);
            if (draggingWorkbenchSplitter_ || splitterHitTest(point.x, point.y)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        const int x = signedLowWord(lParam);
        const int y = signedHighWord(lParam);
        if (splitterHitTest(x, y)) {
            preferredWorkbenchHeight_ = 0;
            relayoutCurrentClient();
            saveUiPreferences();
            return 0;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        const int x = signedLowWord(lParam);
        const int y = signedHighWord(lParam);
        if (splitterHitTest(x, y)) {
            draggingWorkbenchSplitter_ = true;
            splitterDragStartY_ = y;
            splitterDragStartWorkbenchHeight_ = currentWorkbenchHeight_ > 0
                ? currentWorkbenchHeight_
                : preferredWorkbenchHeight_;
            SetCapture(window_);
            SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (draggingWorkbenchSplitter_) {
            RECT clientRect = {};
            GetClientRect(window_, &clientRect);
            const int y = signedHighWord(lParam);
            const int requestedHeight = splitterDragStartWorkbenchHeight_ - (y - splitterDragStartY_);
            const int nextWorkbenchHeight = clampedWorkbenchHeightForClient(
                requestedHeight,
                clientRect.right - clientRect.left,
                clientRect.bottom - clientRect.top);
            if (nextWorkbenchHeight != currentWorkbenchHeight_) {
                preferredWorkbenchHeight_ = nextWorkbenchHeight;
                scheduleSplitterDragFrame(
                    clientRect.right - clientRect.left,
                    clientRect.bottom - clientRect.top,
                    nextWorkbenchHeight);
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (draggingWorkbenchSplitter_) {
            draggingWorkbenchSplitter_ = false;
            if (GetCapture() == window_) {
                ReleaseCapture();
            }
            relayoutCurrentClient();
            saveUiPreferences();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != window_) {
            draggingWorkbenchSplitter_ = false;
        }
        break;
    case WM_NOTIFY: {
        if (const auto result = handleNotifyMessage(lParam); result.has_value()) {
            return *result;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        return handleEditColorMessage(wParam);
    case WM_CTLCOLORSTATIC: {
        if (const auto result = handleStaticColorMessage(wParam, lParam); result.has_value()) {
            return *result;
        }
        break;
    }
    case WM_CTLCOLORBTN:
        return handleButtonColorMessage(wParam);
    case WM_COMMAND: {
        if (const auto result = handleCommandMessage(wParam); result.has_value()) {
            return *result;
        }
        break;
    }
    case WM_TIMER: {
        if (const auto result = handleTimerMessage(wParam); result.has_value()) {
            return *result;
        }
        break;
    }
    case kNativeUiFrameMessage:
        processNativeFrame();
        return 0;
    case kNativeWorkbenchTabRepaintMessage:
        repaintWorkbenchTabControls();
        return 0;
    case kNativeModbusScanDoneMessage:
        handleModbusScanDone();
        return 0;
    case kNativeModbusScanProgressMessage:
        handleModbusScanProgress(reinterpret_cast<NativeModbusScanProgress*>(lParam));
        return 0;
    case kNativeModbusScanDataMessage:
        handleModbusScanDataBatch(reinterpret_cast<NativeModbusScanDataBatch*>(lParam));
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        return handleDestroyMessage();
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

} // namespace svm::win32

#endif
