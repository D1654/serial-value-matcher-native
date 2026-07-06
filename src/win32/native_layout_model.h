#pragma once

#if defined(_WIN32)

#include "win32/native_layout_metrics.h"

#include <array>

namespace svm::win32 {

inline constexpr int kNativeWorkbenchTabCount = 5;

struct NativeLayoutModelInput {
    int clientWidth = 0;
    int clientHeight = 0;
    int requestedWorkbenchHeight = 0;
    int selectedTabIndex = 0;
};

struct NativeTabVisibilityModel {
    bool pageVisible = false;
    bool singleFormatRow = false;
    bool singleHistory = false;
    bool singleSend = false;
    bool singleTimed = false;
    std::array<bool, 10> quickSlots = {};
    bool fileFirstRow = false;
    bool fileSecondRow = false;
    bool scanSection = false;
    bool scanParameterRow = false;
    bool scanProgressRow = false;
    bool scanAnalysisSection = false;
    bool scanTargetRow = false;
    bool scanCandidateRow = false;
    bool settingsRow = false;
};

struct NativeSerialPanelLayout {
    NativeRect bounds;
    NativeRect actionSeparator;
    NativeRect pauseButton;
    NativeRect clearButton;
    bool actionsVisible = false;
};

struct NativeSideHelpLayout {
    NativeRect separator;
    NativeRect frame;
    NativeRect title;
    NativeRect text;
    bool visible = false;
};

struct NativeLogPanelLayout {
    NativeRect bounds;
    NativeRect title;
    LogToolbarLayout toolbar;
    NativeRect logView;
    bool titleVisible = false;
};

struct NativeWorkbenchLayout {
    NativeRect bounds;
    NativeRect splitter;
    NativeRect tabs;
    NativeRect pageBackground;
    NativeRect page;
    NativeTabVisibilityModel visibility;
    int selectedTabIndex = 0;
    int minimumWorkbenchHeight = 0;
    int maximumWorkbenchHeight = 0;
    int minimumLogHeight = 0;
    int workbenchHeight = 0;
    int logHeight = 0;
};

struct NativeStatusLayout {
    NativeRect statusText;
    NativeRect txText;
    NativeRect rxText;
    NativeRect clockText;
    bool countersVisible = false;
    bool clockVisible = false;
};

struct NativeMainLayoutModel {
    int requestedWidth = 0;
    int requestedHeight = 0;
    int width = 0;
    int height = 0;
    bool forcedSmall = false;
    NativeUiMetrics metrics;
    NativeRect mainArea;
    NativeSerialPanelLayout serialPanel;
    NativeSideHelpLayout sideHelp;
    NativeLogPanelLayout logPanel;
    NativeWorkbenchLayout workbench;
    NativeStatusLayout status;
};

NativeMainLayoutModel calculateNativeMainLayoutModel(const NativeLayoutModelInput& input);
bool nativeMainLayoutModelHasStableGeometry(const NativeMainLayoutModel& model);

} // namespace svm::win32

#endif
