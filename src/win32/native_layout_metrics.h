#pragma once

#if defined(_WIN32)

namespace svm::win32 {

inline constexpr int kMinTrackWidth = 760;
inline constexpr int kMinTrackHeight = 520;
inline constexpr int kMinLayoutWidth = 760;
inline constexpr int kMinLayoutHeight = 520;

struct NativeRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    int right() const {
        return x + width;
    }

    int bottom() const {
        return y + height;
    }
};

struct SendControlLayout {
    NativeRect modeLabel;
    NativeRect modeCombo;
    NativeRect encodingLabel;
    NativeRect encodingCombo;
    NativeRect lineEndingLabel;
    NativeRect lineEndingCombo;
    NativeRect historyLabel;
    NativeRect historyCombo;
};

struct LogToolbarLayout {
    NativeRect formatLabel;
    NativeRect formatCombo;
    NativeRect encodingLabel;
    NativeRect encodingCombo;
    NativeRect copyButton;
    NativeRect exportButton;
    NativeRect filterLabel;
    NativeRect filterEdit;
    NativeRect searchLabel;
    NativeRect searchEdit;
    NativeRect findButton;
};

struct MainLayoutProbe {
    int requestedWidth = 0;
    int requestedHeight = 0;
    int width = 0;
    int height = 0;
    bool compact = false;
    bool forcedSmall = false;
    int margin = 0;
    int groupPad = 0;
    int row = 0;
    int connectionWidth = 0;
    int connectionHeight = 0;
    int statusY = 0;
    int contentY = 0;
    int contentHeight = 0;
    int leftWidth = 0;
    int rightWidth = 0;
    int sendHeight = 0;
    int workflowY = 0;
    int workflowHeight = 0;
    int sendInnerWidth = 0;
    int logInnerWidth = 0;
    int logContentHeight = 0;
};

struct NativeUiMetrics {
    bool compact = false;
    bool tight = false;
    int margin = 0;
    int row = 0;
    int gap = 0;
    int labelHeight = 0;
    int titleHeight = 0;
    int statusHeight = 0;
    int sideGap = 0;
    int smallButtonWidth = 0;
    int desiredSideWidth = 0;
    int minSideWidth = 0;
    int desiredWorkHeight = 0;
    int minimumLogHeight = 0;
    int logActionWidth = 0;
};

NativeUiMetrics nativeUiMetricsForSize(int width, int height);
SendControlLayout calculateSendControlLayout(int x, int y, int innerWidth, int row, int gap, int labelHeight);
LogToolbarLayout calculateLogToolbarLayout(int x, int y, int innerWidth, int row, int gap, int preferredActionWidth);
bool logToolbarLayoutIsSane(int innerWidth);
bool sendControlLayoutIsSane(int innerWidth);
bool scanTabLayoutIsSane(int pageWidth, int pageHeight);
MainLayoutProbe calculateMainLayoutProbe(int requestedWidth, int requestedHeight);
bool mainLayoutProbeHasStableGeometry(const MainLayoutProbe& probe);
bool mainLayoutProbeSupportsFullInteraction(const MainLayoutProbe& probe);
bool mainLayoutProbeIsStableAtSize(int width, int height);
bool mainLayoutProbeIsFullyUsableAtSize(int width, int height);

} // namespace svm::win32

#endif
