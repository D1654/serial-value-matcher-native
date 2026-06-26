#if defined(_WIN32)

#include "win32/native_layout_metrics.h"

#include <cassert>
#include <iostream>

namespace {

void compactDesktopMetricsStayUsable() {
    const auto metrics = svm::win32::nativeUiMetricsForSize(800, 600);
    assert(metrics.compact);
    assert(metrics.tight);
    assert(metrics.desiredWorkHeight == 230);
    assert(metrics.minimumLogHeight == 150);
    assert(svm::win32::sendControlLayoutIsSane(320));
    assert(svm::win32::logToolbarLayoutIsSane(360));
    assert(svm::win32::scanTabLayoutIsSane(554, 132));
}

void standardDesktopMetricsStayUsable() {
    const auto metrics = svm::win32::nativeUiMetricsForSize(1366, 768);
    assert(!metrics.compact);
    assert(!metrics.tight);
    assert(metrics.desiredWorkHeight == 236);
    assert(metrics.minimumLogHeight == 210);
    assert(svm::win32::mainLayoutProbeIsFullyUsableAtSize(760, 520));
    assert(svm::win32::mainLayoutProbeIsFullyUsableAtSize(1040, 720));
    assert(svm::win32::mainLayoutProbeIsFullyUsableAtSize(1366, 768));
}

void smallWindowGeometryRemainsStable() {
    assert(svm::win32::mainLayoutProbeIsStableAtSize(640, 400));
    assert(svm::win32::mainLayoutProbeIsStableAtSize(320, 240));
    assert(svm::win32::mainLayoutProbeIsStableAtSize(1, 1));
}

} // namespace

int main() {
    compactDesktopMetricsStayUsable();
    standardDesktopMetricsStayUsable();
    smallWindowGeometryRemainsStable();

    std::cout << "native_layout_metrics_tests passed\n";
    return 0;
}

#endif
