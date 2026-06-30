#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "win32/native_paint_policy.h"

#include <cstdio>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        return false;
    }
    return true;
}

bool testLivePolicyAvoidsEraseAndImmediatePaint() {
    const UINT flags = svm::win32::nativeLiveRegionRedrawFlags();
    return expect((flags & RDW_INVALIDATE) != 0, "live policy should invalidate")
        && expect((flags & RDW_ALLCHILDREN) != 0, "live policy should include children")
        && expect((flags & RDW_NOERASE) != 0, "live policy should avoid erase")
        && expect(!svm::win32::nativeRedrawFlagsForceImmediatePaint(flags), "live policy should not force immediate paint")
        && expect(!svm::win32::nativeRedrawFlagsEraseBackground(flags), "live policy should not erase background");
}

bool testFullRefreshIsExplicitlySynchronous() {
    const UINT flags = svm::win32::nativeFullRefreshRedrawFlags();
    return expect((flags & RDW_INVALIDATE) != 0, "full refresh should invalidate")
        && expect((flags & RDW_ERASE) != 0, "full refresh should erase")
        && expect((flags & RDW_ALLCHILDREN) != 0, "full refresh should include children")
        && expect(svm::win32::nativeRedrawFlagsForceImmediatePaint(flags), "full refresh should force immediate paint")
        && expect(svm::win32::nativeRedrawFlagsEraseBackground(flags), "full refresh should erase background");
}

bool testDraggingWorkbenchPoliciesAvoidSynchronousErase() {
    const UINT tabFlags = svm::win32::nativeWorkbenchTabRedrawFlags(true);
    const UINT areaFlags = svm::win32::nativeWorkbenchAreaRedrawFlags(true);
    const UINT backgroundFlags = svm::win32::nativeWorkbenchBackgroundPositionFlags(true);
    return expect((tabFlags & RDW_NOERASE) != 0, "dragging tab policy should avoid erase")
        && expect(!svm::win32::nativeRedrawFlagsForceImmediatePaint(tabFlags), "dragging tab policy should not force paint")
        && expect((areaFlags & RDW_NOERASE) != 0, "dragging area policy should avoid erase")
        && expect(!svm::win32::nativeRedrawFlagsForceImmediatePaint(areaFlags), "dragging area policy should not force paint")
        && expect((backgroundFlags & SWP_NOREDRAW) != 0, "dragging background policy should avoid redraw");
}

bool testLogFlushDoesNotForceImmediatePaint() {
    const UINT flags = svm::win32::nativeLogFlushRedrawFlags();
    return expect((flags & RDW_INVALIDATE) != 0, "log flush should invalidate")
        && expect((flags & RDW_ERASE) != 0, "log flush may erase")
        && expect((flags & RDW_ALLCHILDREN) != 0, "log flush should include children")
        && expect(!svm::win32::nativeRedrawFlagsForceImmediatePaint(flags), "log flush should not force immediate paint");
}

} // namespace

int main() {
    if (!testLivePolicyAvoidsEraseAndImmediatePaint()
        || !testFullRefreshIsExplicitlySynchronous()
        || !testDraggingWorkbenchPoliciesAvoidSynchronousErase()
        || !testLogFlushDoesNotForceImmediatePaint()) {
        return 1;
    }

    std::puts("native_paint_policy_tests passed");
    return 0;
}

#else

int main() {
    return 0;
}

#endif
