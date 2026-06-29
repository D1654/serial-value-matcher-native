#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "win32/native_control_utils.h"
#include "win32/native_layout_transaction.h"

#include <cstdio>

namespace {

constexpr const wchar_t* kTestWindowClass = L"SvmNativeLayoutTransactionTestWindow";

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        return false;
    }
    return true;
}

bool ensureTestWindowClass(HINSTANCE instance) {
    WNDCLASSW existing = {};
    if (GetClassInfoW(instance, kTestWindowClass, &existing) != FALSE) {
        return true;
    }

    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTestWindowClass;
    return RegisterClassW(&windowClass) != 0;
}

HWND createChild(HWND parent, int x, int y, int width, int height, bool visible = true) {
    const DWORD style = WS_CHILD | (visible ? WS_VISIBLE : 0);
    return CreateWindowExW(
        0,
        L"STATIC",
        L"",
        style,
        x,
        y,
        width,
        height,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
}

class TestWindows final {
public:
    TestWindows() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (!ensureTestWindowClass(instance)) {
            return;
        }

        parent = CreateWindowExW(
            0,
            kTestWindowClass,
            L"",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            320,
            240,
            nullptr,
            nullptr,
            instance,
            nullptr);
        if (parent == nullptr) {
            return;
        }

        childA = createChild(parent, 10, 10, 30, 20);
        childB = createChild(parent, 60, 10, 30, 20);
        hiddenChild = createChild(parent, 100, 10, 30, 20, false);
    }

    ~TestWindows() {
        if (parent != nullptr) {
            DestroyWindow(parent);
        }
    }

    bool valid() const noexcept {
        return parent != nullptr && childA != nullptr && childB != nullptr && hiddenChild != nullptr;
    }

    HWND parent = nullptr;
    HWND childA = nullptr;
    HWND childB = nullptr;
    HWND hiddenChild = nullptr;
};

bool testChangedGeometryMove() {
    TestWindows windows;
    if (!expect(windows.valid(), "test windows should be created")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction transaction(FALSE);
    transaction.move(windows.childA, 20, 25, 44, 31);
    const svm::win32::NativeLayoutTransactionStats stats = transaction.commit();
    return expect(stats.requestedMoves == 1, "changed move should be requested")
        && expect(stats.appliedMoves == 1, "changed move should be applied")
        && expect(stats.skippedMoves == 0, "changed move should not be skipped")
        && expect(stats.failedMoves == 0, "changed move should not fail")
        && expect(svm::win32::nativeControlGeometryMatches(windows.childA, 20, 25, 44, 31), "changed move geometry should match");
}

bool testRedundantMoveIsSkipped() {
    TestWindows windows;
    if (!expect(windows.valid(), "test windows should be created")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction transaction(FALSE);
    transaction.move(windows.childA, 10, 10, 30, 20);
    const svm::win32::NativeLayoutTransactionStats stats = transaction.commit();
    return expect(stats.requestedMoves == 1, "redundant move should be requested")
        && expect(stats.appliedMoves == 0, "redundant move should not be applied")
        && expect(stats.skippedMoves == 1, "redundant move should be skipped")
        && expect(stats.failedMoves == 0, "redundant move should not fail");
}

bool testBatchUsesDeferredPositioning() {
    TestWindows windows;
    if (!expect(windows.valid(), "test windows should be created")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction transaction(FALSE);
    transaction.move(windows.childA, 18, 18, 40, 24);
    transaction.move(windows.childB, 70, 18, 40, 24);
    const svm::win32::NativeLayoutTransactionStats stats = transaction.commit();
    return expect(stats.requestedMoves == 2, "batch moves should be requested")
        && expect(stats.appliedMoves == 2, "batch moves should be applied")
        && expect(stats.skippedMoves == 0, "batch moves should not be skipped")
        && expect(stats.failedMoves == 0, "batch moves should not fail")
        && expect(stats.usedDeferredPositioning, "batch moves should use deferred positioning")
        && expect(svm::win32::nativeControlGeometryMatches(windows.childA, 18, 18, 40, 24), "batch child A geometry should match")
        && expect(svm::win32::nativeControlGeometryMatches(windows.childB, 70, 18, 40, 24), "batch child B geometry should match");
}

bool testVisibilityDiffing() {
    TestWindows windows;
    if (!expect(windows.valid(), "test windows should be created")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction hideTransaction(FALSE);
    hideTransaction.show(windows.childA, false);
    const svm::win32::NativeLayoutTransactionStats hideStats = hideTransaction.commit();
    if (!expect(hideStats.appliedVisibility == 1, "hide should apply visibility")
        || !expect(!svm::win32::nativeControlIsVisible(windows.childA), "child should be hidden")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction noOpTransaction(FALSE);
    noOpTransaction.show(windows.childA, false);
    const svm::win32::NativeLayoutTransactionStats noOpStats = noOpTransaction.commit();
    if (!expect(noOpStats.appliedVisibility == 0, "same hidden state should not apply")
        || !expect(noOpStats.skippedVisibility == 1, "same hidden state should be skipped")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction showTransaction(FALSE);
    showTransaction.showFast(windows.childA, true);
    const svm::win32::NativeLayoutTransactionStats showStats = showTransaction.commit();
    return expect(showStats.appliedVisibility == 1, "fast show should apply visibility")
        && expect(svm::win32::nativeControlIsVisible(windows.childA), "child should be shown");
}

bool testMoveTopShowsHiddenControl() {
    TestWindows windows;
    if (!expect(windows.valid(), "test windows should be created")) {
        return false;
    }

    svm::win32::NativeLayoutTransaction transaction(FALSE);
    transaction.moveTop(windows.hiddenChild, 100, 10, 30, 20);
    const svm::win32::NativeLayoutTransactionStats stats = transaction.commit();
    return expect(stats.requestedMoves == 1, "moveTop should be requested")
        && expect(stats.appliedMoves == 1, "moveTop should be applied")
        && expect(stats.failedMoves == 0, "moveTop should not fail")
        && expect(svm::win32::nativeControlIsVisible(windows.hiddenChild), "moveTop should show the control");
}

} // namespace

int main() {
    if (!testChangedGeometryMove()
        || !testRedundantMoveIsSkipped()
        || !testBatchUsesDeferredPositioning()
        || !testVisibilityDiffing()
        || !testMoveTopShowsHiddenControl()) {
        return 1;
    }

    std::puts("native_ui_layout_transaction_tests passed");
    return 0;
}

#else

int main() {
    return 0;
}

#endif
