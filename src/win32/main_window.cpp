#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_layout_metrics.h"

namespace svm::win32 {

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
        layoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;
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
    case kNativeModbusScanDoneMessage:
        handleModbusScanDone(reinterpret_cast<NativeModbusScanResult*>(lParam));
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
