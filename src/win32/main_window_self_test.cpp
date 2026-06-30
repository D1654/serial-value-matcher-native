#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_analysis_workflow.h"
#include "win32/native_layout_metrics.h"
#include "win32/native_modbus_scan_request.h"
#include "win32/native_progress_control.h"
#include "win32/native_send_codec.h"
#include "win32/native_time_utils.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_types.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <commctrl.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

constexpr COLORREF kFormBackgroundColor = RGB(228, 228, 228);

const wchar_t* tx(T id) {
    return uiText(id);
}

void writeSelfTestTrace(const char* message) {
    char path[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("SVM_NATIVE_SELF_TEST_LOG", path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    std::ofstream output(path, std::ios::app);
    if (output) {
        output << message << '\n';
    }
}

bool controlIsVisible(HWND control) {
    if (control == nullptr || IsWindowVisible(control) == FALSE) {
        return false;
    }
    RECT rect = {};
    if (GetWindowRect(control, &rect) == FALSE) {
        return false;
    }
    return rect.right > rect.left && rect.bottom > rect.top;
}

int controlHeight(HWND control) {
    RECT rect = {};
    if (control == nullptr || GetWindowRect(control, &rect) == FALSE) {
        return 0;
    }
    return std::max(0L, rect.bottom - rect.top);
}

int controlTop(HWND control) {
    RECT rect = {};
    if (control == nullptr || GetWindowRect(control, &rect) == FALSE) {
        return 0;
    }
    return static_cast<int>(rect.top);
}

int controlBottom(HWND control) {
    RECT rect = {};
    if (control == nullptr || GetWindowRect(control, &rect) == FALSE) {
        return 0;
    }
    return static_cast<int>(rect.bottom);
}

bool controlsAreVisible(std::initializer_list<HWND> controls) {
    for (HWND control : controls) {
        if (!controlIsVisible(control)) {
            return false;
        }
    }
    return true;
}

bool serialStateSelfTestProbe() {
    NativeSerialIoState state;
    if (!state.allowsManualSend() || !state.allowsFileSend() || !state.allowsModbusScan()) {
        return false;
    }
    if (!state.tryAcquire(NativeSerialIoOwner::ModbusScan)) {
        return false;
    }
    if (state.allowsManualSend() || state.allowsFileSend() || state.allowsSerialPoll() || !state.shouldDeferDisconnect()) {
        return false;
    }
    if (state.release(NativeSerialIoOwner::ManualSend)) {
        return false;
    }
    return state.release(NativeSerialIoOwner::ModbusScan)
        && state.allowsManualSend()
        && state.allowsSerialPoll();
}

bool logEvidenceSelfTestProbe() {
    NativeLogFilterState filter;
    const NativeLogFilterUpdate update = filter.setFilterText(L"Rx");
    if (!update.changed || filter.loweredFilterText() != L"rx") {
        return false;
    }
    if (!containsCaseInsensitive(L"[RX] 01 03", L"rx")) {
        return false;
    }
    if (sanitizeLogText(L"A\r\n\tB") != L"A\\r\\n\\tB") {
        return false;
    }
    if (clipRenderedLogLine(L"abcdef", 4, L"...") != L"a...") {
        return false;
    }
    const NativeLogSearchResult first = filter.findNext(L"[TX] one\n[RX] two", L"rx");
    return first.found && first.length == 2;
}

NativeSendCodecErrors selfTestSendCodecErrors() {
    NativeSendCodecErrors errors;
    errors.hexInvalidChar = L"hex invalid";
    errors.hexOddNibble = L"hex odd";
    errors.invalidDecimal = L"decimal invalid";
    errors.invalidBinary = L"binary invalid";
    errors.textEncodingFailed = L"encoding failed";
    return errors;
}

bool sendWorkflowSelfTestProbe(const std::filesystem::path& tempDirectory) {
    NativeSendPayloadOptions options;
    options.mode = 1;
    options.lineEnding = 3;
    const NativeSendPayloadResult payload = nativeBuildSendPayload(L"AA", options, selfTestSendCodecErrors());
    if (!payload.errorText.empty() || payload.payload != std::vector<std::uint8_t>({0xAA, '\r', '\n'})) {
        return false;
    }

    NativeSendControlState controls;
    controls.setTimedSendEnabled(true);
    const NativeTimedSendTimerDecision timer = controls.timerDecision(true, true, 1);
    if (!timer.shouldRun || timer.periodMs != kNativeMinTimedSendPeriodMs) {
        return false;
    }
    if (!controls.isQuickSendIndexValid(9, 10) || controls.isQuickSendIndexValid(10, 10) || controls.isQuickSendTextUsable(L"")) {
        return false;
    }

    const std::filesystem::path filePath = tempDirectory / L"svm-native-self-test-file-send.bin";
    {
        std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
        output.put('\x01');
        output.put('\x02');
        output.put('\x03');
    }

    NativeFileSendState fileSend;
    const NativeFileSendOpenResult open = fileSend.open(filePath);
    if (!open.ok() || open.totalBytes != 3 || !fileSend.active()) {
        std::filesystem::remove(filePath);
        return false;
    }
    const NativeFileSendChunk chunk = fileSend.readNextChunk(8);
    if (!chunk.ready() || chunk.bytes != std::vector<std::uint8_t>({1, 2, 3}) || fileSend.done()) {
        std::filesystem::remove(filePath);
        return false;
    }
    fileSend.markBytesWritten(chunk.bytes.size());
    const bool done = fileSend.done() && fileSend.progressPermille() == 1000;
    fileSend.close();
    std::filesystem::remove(filePath);
    return done;
}

bool modbusAnalysisReportSelfTestProbe() {
    NativeModbusScanRequestInput input;
    input.slaveId = 2;
    input.functionCode = static_cast<core::Byte>(core::modbus::ModbusReadFunction::InputRegisters);
    input.startAddress = 100;
    input.endAddress = 103;
    input.blockSize = 2;
    input.scanSessionId = "scan-self-test";
    input.startedAtUtc = "created-at";
    const NativeModbusScanRequestResult request = nativeBuildModbusScanRequest(input);
    if (!request.ok || request.request.plan.requestCount() != 2 || request.request.execution.session.status != "running") {
        return false;
    }

    native_storage::ScanSessionRecord session;
    session.sessionId = "scan-self-test";
    session.slaveId = 2;
    session.functionCode = 4;
    session.startAddress = 100;
    session.endAddress = 103;
    session.blockSize = 2;
    session.status = "completed";

    std::vector<native_storage::ScanObservationRecord> observations;
    native_storage::ScanObservationRecord observation;
    observation.id = 41;
    observation.sessionId = session.sessionId;
    observation.slaveId = session.slaveId;
    observation.functionCode = session.functionCode;
    observation.address = 100;
    observation.value = 1;
    observation.observedAtUtc = "observed-at";
    observations.push_back(observation);

    const NativeCandidateAnalysisBuildResult analysis = nativeBuildCandidateAnalysisRun(
        session,
        observations,
        "mode",
        1.0,
        "",
        0.0,
        "match-self-test",
        "sampled-at",
        "created-at",
        "observed-at");
    if (!analysis.success || analysis.candidates.empty()) {
        return false;
    }

    native_storage::ProtocolFieldRuleRecord rule;
    rule.ruleId = "rule-self-test";
    rule.fieldName = "mode";
    rule.candidateType = "EnumMap";
    rule.wordOrder = "HighWordFirst";
    rule.byteOrder = "BigEndian";
    rule.slaveId = session.slaveId;
    rule.functionCode = session.functionCode;
    rule.startAddress = 100;
    rule.registerCount = 1;
    rule.scaleMultiplier = 1.0;
    rule.interpretationMap = "0=停止\n1=运行";
    const NativeRuleVerificationBuildResult verification = nativeBuildRuleVerificationResult(
        session,
        {rule},
        observations,
        "verify-self-test",
        "created-at");
    if (verification.run.verifiedCount != 1 || verification.results.size() != 1) {
        return false;
    }
    if (verification.results.front().interpretationText != "枚举解释：运行。") {
        return false;
    }

    const std::string markdown = nativeRenderRuleVerificationMarkdownReport(verification.run, verification.results);
    return markdown.find("# 协议规则验证报告") == 0
        && markdown.find("verify-self-test") != std::string::npos
        && markdown.find("枚举解释：运行。") != std::string::npos;
}

} // namespace

bool NativeMainWindow::runSelfTest() {
    const auto fail = [](const char* step) {
        writeSelfTestTrace(step);
        return false;
    };
    const std::wstring expectedChinese = {
        static_cast<wchar_t>(0x4E32),
        static_cast<wchar_t>(0x53E3),
        static_cast<wchar_t>(0x503C),
        static_cast<wchar_t>(0x5339),
        static_cast<wchar_t>(0x914D),
        static_cast<wchar_t>(0x5668),
    };
    constexpr char kExpectedUtf8[] = "\xE4\xB8\xB2\xE5\x8F\xA3\xE5\x80\xBC\xE5\x8C\xB9\xE9\x85\x8D\xE5\x99\xA8";
    if (std::wstring(tx(T::SelfTestText)) != expectedChinese
        || utf8ToWide(kExpectedUtf8) != expectedChinese
        || wideToUtf8(expectedChinese) != kExpectedUtf8) {
        return fail("unicode-roundtrip");
    }

    SerialOpenOptions options;
    options.portName = "COM1";
    if (!validateSerialOpenOptions(options).ok) {
        return fail("serial-options");
    }
    if (makeWin32DevicePath("COM10") != R"(\\.\COM10)") {
        return fail("serial-device-path");
    }
    if (!logToolbarLayoutIsSane(360)) {
        return fail("log-toolbar-360");
    }
    if (!logToolbarLayoutIsSane(554)) {
        return fail("log-toolbar-554");
    }
    if (!sendControlLayoutIsSane(320)) {
        return fail("send-layout-320");
    }
    if (!sendControlLayoutIsSane(428)) {
        return fail("send-layout-428");
    }
    if (!scanTabLayoutIsSane(554, 132)) {
        return fail("scan-tab-layout-554x132");
    }
    if (!nativeProgressStyleHasVisibleFrame(kFormBackgroundColor)) {
        return fail("progress-border-style");
    }
    if (!mainLayoutProbeIsFullyUsableAtSize(kMinTrackWidth, kMinTrackHeight)) {
        return fail("main-layout-min");
    }
    if (!mainLayoutProbeIsFullyUsableAtSize(1040, 720)) {
        return fail("main-layout-1040x720");
    }
    if (!mainLayoutProbeIsFullyUsableAtSize(1366, 768)) {
        return fail("main-layout-1366x768");
    }
    if (!mainLayoutProbeIsStableAtSize(640, 400)) {
        return fail("main-layout-640x400");
    }
    if (!mainLayoutProbeIsStableAtSize(480, 320)) {
        return fail("main-layout-480x320");
    }
    if (!mainLayoutProbeIsStableAtSize(320, 240)) {
        return fail("main-layout-320x240");
    }
    if (!mainLayoutProbeIsStableAtSize(1, 1)) {
        return fail("main-layout-1x1");
    }

    wchar_t tempPathBuffer[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPathBuffer) == 0) {
        return fail("temp-path");
    }
    const std::filesystem::path tempDirectory(tempPathBuffer);

    if (!serialStateSelfTestProbe()) {
        return fail("functional-serial-state");
    }
    if (!logEvidenceSelfTestProbe()) {
        return fail("functional-log-evidence");
    }
    if (!sendWorkflowSelfTestProbe(tempDirectory)) {
        return fail("functional-send-workflow");
    }
    if (!modbusAnalysisReportSelfTestProbe()) {
        return fail("functional-modbus-analysis-report");
    }

    const auto storePath = tempDirectory / L"svm-native-win32-self-test";
    std::filesystem::remove_all(storePath);
    native_storage::NativeSessionStore store;
    if (!store.open(storePath)) {
        return fail("store-open");
    }

    native_storage::RawIoEvent event;
    event.sessionId = "self-test";
    event.direction = "Tx";
    event.timestampUtc = nativeUtcTimestampText();
    event.endpoint = "COM1";
    event.payload = {0x01, 0x03, 0x00, 0x00};

    native_storage::SerialProfile profile;
    profile.portName = "COM1";
    profile.baudRate = 115200;
    profile.dataBits = 8;
    profile.parity = "None";
    profile.stopBits = "One";
    profile.flowControl = "None";
    profile.updatedAtUtc = nativeUtcTimestampText();

    native_storage::SendHistoryEntry history;
    history.content = "AT+\xE6\xB5\x8B\xE8\xAF\x95";
    history.payloadMode = 0;
    history.lineEnding = 0;
    history.textEncodingCodePage = CP_UTF8;
    history.sentAtUtc = nativeUtcTimestampText();

    native_storage::UiPreferences preferences;
    preferences.logThemeIndex = 1;
    preferences.logFormat = 4;
    preferences.logEncodingCodePage = CP_UTF8;
    preferences.showLogTimestamps = false;
    preferences.updatedAtUtc = nativeUtcTimestampText();

    bool ok = true;
    const auto storageFail = [&](const char* step) {
        ok = false;
        writeSelfTestTrace(step);
    };
    if (!store.appendRawEvent(event)) {
        storageFail("storage-append-raw-event");
    } else if (store.rawEventCount() != 1) {
        storageFail("storage-raw-event-count");
    } else if (!store.saveSerialProfile(profile)) {
        storageFail("storage-save-profile");
    } else if (!store.latestSerialProfile().has_value()) {
        storageFail("storage-read-profile");
    } else if (!store.saveSendHistory(history)) {
        storageFail("storage-save-history");
    } else if (store.recentSendHistory(1).empty()) {
        storageFail("storage-read-history");
    } else if (!store.saveUiPreferences(preferences)) {
        storageFail("storage-save-preferences");
    } else if (!store.latestUiPreferences().has_value()) {
        storageFail("storage-read-preferences");
    }
    std::filesystem::remove_all(storePath);
    if (!ok) {
        return false;
    }
    writeSelfTestTrace("ok");
    return ok;
}

bool NativeMainWindow::runUiPerformanceTest() {
    const auto fail = [](const char* step) {
        writeSelfTestTrace(step);
        return false;
    };

    NativeMainWindow window;
    if (!window.create(GetModuleHandleW(nullptr))) {
        return fail("ui-perf-create-window");
    }

    window.show(SW_SHOWNOACTIVATE);
    window.layoutControls(kMinTrackWidth, kMinTrackHeight);
    const auto selectWorkbenchTab = [&](int tabIndex) {
        if (TabCtrl_SetCurSel(window.workTabs_, tabIndex) < 0) {
            return false;
        }
        NMHDR notification = {};
        notification.hwndFrom = window.workTabs_;
        notification.idFrom = IDC_WORK_TABS;
        notification.code = TCN_SELCHANGE;
        SendMessageW(window.window_, WM_NOTIFY, IDC_WORK_TABS, reinterpret_cast<LPARAM>(&notification));
        return true;
    };
    const auto keyWorkbenchControlsVisible = [&]() {
        if (!selectWorkbenchTab(0)
            || !controlsAreVisible({
                window.sendModeCombo_,
                window.textEncodingCombo_,
                window.lineEndingCombo_,
                window.historyCombo_,
                window.sendEdit_,
                window.sendButton_,
                window.timedSendCheck_,
                window.timedPeriodEdit_,
            })) {
            return false;
        }

        if (!selectWorkbenchTab(1)) {
            return false;
        }
        for (std::size_t index = 0; index < window.quickSendEdits_.size(); ++index) {
            if (!controlIsVisible(window.quickSendEdits_[index]) || !controlIsVisible(window.quickSendButtons_[index])) {
                return false;
            }
        }

        if (!selectWorkbenchTab(2)
            || !controlsAreVisible({
                window.filePathEdit_,
                window.fileBrowseButton_,
                window.fileSendButton_,
                window.fileStopButton_,
                window.fileDelayCombo_,
                window.fileProgress_,
            })) {
            return false;
        }

        if (!selectWorkbenchTab(3)
            || !controlsAreVisible({
                window.scanSlaveEdit_,
                window.scanFunctionCombo_,
                window.scanStartEdit_,
                window.scanEndEdit_,
                window.modbusButton_,
                window.modbusProgress_,
                window.targetLabelEdit_,
                window.targetValueEdit_,
                window.targetUnitEdit_,
                window.toleranceEdit_,
                window.candidateCombo_,
                window.analysisButton_,
                window.ruleVerifyButton_,
                window.exportReportButton_,
            })) {
            return false;
        }

        return selectWorkbenchTab(4)
            && controlsAreVisible({
                window.logCacheCombo_,
                window.rawEventRetentionCombo_,
            });
    };
    const auto sideHelpTracksWorkbenchTabs = [&]() {
        const std::array<std::pair<int, T>, 5> expected = {{
            {0, T::SideHelpSingle},
            {1, T::SideHelpQuick},
            {2, T::SideHelpFile},
            {3, T::SideHelpScan},
            {4, T::SideHelpSettings},
        }};
        for (const auto& [tabIndex, helpText] : expected) {
            if (!selectWorkbenchTab(tabIndex) || window.controlText(window.sideHelpText_) != tx(helpText)) {
                return false;
            }
        }
        return true;
    };
    const auto singleSendHasComfortableGap = [&]() {
        window.layoutControls(1212, 753);
        if (!selectWorkbenchTab(0)) {
            window.layoutControls(kMinTrackWidth, kMinTrackHeight);
            return false;
        }
        const bool hasGap = controlTop(window.sendEdit_) - controlTop(window.sendModeCombo_) >= 34;
        window.layoutControls(kMinTrackWidth, kMinTrackHeight);
        return hasGap;
    };
    const auto sideHelpIsBottomAnchored = [&]() {
        window.layoutControls(1212, 753);
        if (!selectWorkbenchTab(3)) {
            window.layoutControls(kMinTrackWidth, kMinTrackHeight);
            return false;
        }
        const bool anchoredAboveStatus = controlTop(window.statusText_) - controlBottom(window.sideHelpFrame_) >= 0
            && controlTop(window.statusText_) - controlBottom(window.sideHelpFrame_) <= 8;
        const bool separatedFromActions = controlTop(window.sideHelpFrame_) - controlBottom(window.clearButton_) >= 80;
        const bool readable = controlIsVisible(window.sideHelpText_) && controlHeight(window.sideHelpText_) >= 96;
        window.layoutControls(kMinTrackWidth, kMinTrackHeight);
        return anchoredAboveStatus && separatedFromActions && readable;
    };
    const auto splitterDragSkipsRedundantLayouts = [&]() {
        window.processNativeFrame();
        const int originalPreferredHeight = window.preferredWorkbenchHeight_;
        const int startX = (window.workbenchSplitterRect_.left + window.workbenchSplitterRect_.right) / 2;
        const int startY = (window.workbenchSplitterRect_.top + window.workbenchSplitterRect_.bottom) / 2;
        RECT clientRect = {};
        GetClientRect(window.window_, &clientRect);
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const std::uint64_t beforeLayouts = window.layoutPassCount_;

        window.draggingWorkbenchSplitter_ = true;
        window.splitterDragStartY_ = startY;
        window.splitterDragStartWorkbenchHeight_ = window.currentWorkbenchHeight_;
        SendMessageW(window.window_, WM_MOUSEMOVE, 0, MAKELPARAM(startX, startY));
        if (window.layoutPassCount_ != beforeLayouts) {
            window.draggingWorkbenchSplitter_ = false;
            return false;
        }

        const int intermediateY = startY - 18;
        const int latestY = startY - 24;
        SendMessageW(window.window_, WM_MOUSEMOVE, 0, MAKELPARAM(startX, intermediateY));
        SendMessageW(window.window_, WM_MOUSEMOVE, 0, MAKELPARAM(startX, latestY));
        if (window.layoutPassCount_ != beforeLayouts) {
            window.draggingWorkbenchSplitter_ = false;
            return false;
        }
        const int expectedLatestHeight = window.clampedWorkbenchHeightForClient(
            window.splitterDragStartWorkbenchHeight_ - (latestY - window.splitterDragStartY_),
            clientWidth,
            clientHeight);
        window.processNativeFrame();
        const bool changedOnce = window.layoutPassCount_ == beforeLayouts + 1
            && window.currentWorkbenchHeight_ == expectedLatestHeight
            && window.preferredWorkbenchHeight_ == expectedLatestHeight;
        window.draggingWorkbenchSplitter_ = false;
        window.preferredWorkbenchHeight_ = originalPreferredHeight;
        window.relayoutCurrentClient();
        return changedOnce;
    };
    if (!controlsAreVisible({
            window.serialPanelTitle_,
            window.portCombo_,
            window.refreshButton_,
            window.saveProfileButton_,
            window.baudCombo_,
            window.stopBitsCombo_,
            window.dataBitsCombo_,
            window.parityCombo_,
            window.flowControlCombo_,
            window.connectButton_,
            window.dtrCheck_,
            window.rtsCheck_,
            window.autoReconnectCheck_,
            window.logPanelTitle_,
            window.logFormatCombo_,
            window.logEncodingCombo_,
            window.logFilterEdit_,
            window.logSearchEdit_,
            window.findLogButton_,
            window.copyLogButton_,
            window.exportLogButton_,
            window.receiveLog_,
        })) {
        return fail("ui-visible-fixed-controls");
    }
    if (!keyWorkbenchControlsVisible()) {
        return fail("ui-visible-workbench-controls");
    }
    if (!sideHelpTracksWorkbenchTabs()) {
        return fail("ui-side-help-tab-text");
    }
    if (!singleSendHasComfortableGap()) {
        return fail("ui-single-send-comfortable-gap");
    }
    if (!sideHelpIsBottomAnchored()) {
        return fail("ui-side-help-bottom-anchored");
    }
    if (!controlIsVisible(window.sideHelpText_) || controlHeight(window.sideHelpText_) < 108) {
        return fail("ui-side-help-readable-height");
    }
    if (window.workbenchSplitterRect_.bottom - window.workbenchSplitterRect_.top < 10
        || !window.splitterHitTest(
            (window.workbenchSplitterRect_.left + window.workbenchSplitterRect_.right) / 2,
            (window.workbenchSplitterRect_.top + window.workbenchSplitterRect_.bottom) / 2)) {
        return fail("ui-workbench-splitter-hit-target");
    }
    if (!splitterDragSkipsRedundantLayouts()) {
        return fail("ui-workbench-splitter-drag-layout");
    }

    constexpr int kIterations = 300;
    constexpr auto kMaxElapsed = std::chrono::milliseconds(12000);
    const std::uint64_t baselineLayoutPasses = window.layoutPassCount_;
    const std::uint64_t baselineLayoutRevision = window.workbenchTabState_.layoutRevision();
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < kIterations; ++index) {
        const int tabIndex = index % 5;
        TabCtrl_SetCurSel(window.workTabs_, tabIndex);
        NMHDR notification = {};
        notification.hwndFrom = window.workTabs_;
        notification.idFrom = IDC_WORK_TABS;
        notification.code = TCN_SELCHANGE;
        SendMessageW(window.window_, WM_NOTIFY, IDC_WORK_TABS, reinterpret_cast<LPARAM>(&notification));
    }
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    bool ok = true;
    if (window.layoutPassCount_ != baselineLayoutPasses) {
        writeSelfTestTrace("ui-perf-layout-regression");
        ok = false;
    }
    if (window.workbenchTabState_.layoutRevision() != baselineLayoutRevision) {
        writeSelfTestTrace("ui-perf-revision-regression");
        ok = false;
    }
    if (window.workbenchTabState_.applyCount() < static_cast<std::uint64_t>(kIterations)) {
        writeSelfTestTrace("ui-perf-apply-count");
        ok = false;
    }
    if (elapsedMs > kMaxElapsed) {
        writeSelfTestTrace("ui-perf-too-slow");
        ok = false;
    }

    window.clearLog();
    constexpr int kLogIterations = 1200;
    constexpr auto kMaxLogElapsed = std::chrono::milliseconds(12000);
    const auto logStart = std::chrono::steady_clock::now();
    for (int index = 0; index < kLogIterations; ++index) {
        window.appendLog(NativeLogKind::Rx, L"[RX] 01 03 02 00 2A 39 84");
    }
    window.flushPendingLogEntries();
    const auto logElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - logStart);
    if (!window.pendingLogLines_.empty()) {
        writeSelfTestTrace("ui-perf-log-pending");
        ok = false;
    }
    if (window.visibleLogLineCount_ != static_cast<std::size_t>(kLogIterations)) {
        writeSelfTestTrace("ui-perf-log-line-count");
        ok = false;
    }
    if (window.logFlushPassCount_ == 0) {
        writeSelfTestTrace("ui-perf-log-no-flush");
        ok = false;
    }
    if (logElapsedMs > kMaxLogElapsed) {
        writeSelfTestTrace("ui-perf-log-too-slow");
        ok = false;
    }

    window.clearLog();
    const std::uint64_t trimRebuildBaseline = window.logRebuildPassCount_;
    const std::size_t trimStressLines = window.logEntryLimit_ + 800;
    for (std::size_t index = 0; index < trimStressLines; ++index) {
        window.appendLog(NativeLogKind::Rx, L"[RX] 01");
    }
    window.flushPendingLogEntries();
    const std::uint64_t trimRebuildCount = window.logRebuildPassCount_ - trimRebuildBaseline;
    if (window.logEntries_.size() > window.logEntryLimit_) {
        writeSelfTestTrace("ui-perf-log-entry-limit");
        ok = false;
    }
    if (trimRebuildCount > 8) {
        writeSelfTestTrace("ui-perf-log-rebuild-thrash");
        ok = false;
    }

    char message[256] = {};
    std::snprintf(
        message,
        sizeof(message),
        "ui-perf %s tabs=%d tab-ms=%lld layout-pass=%llu apply=%llu revision=%llu log-lines=%d log-ms=%lld log-flush=%llu log-rebuild=%llu",
        ok ? "ok" : "failed",
        kIterations,
        static_cast<long long>(elapsedMs.count()),
        static_cast<unsigned long long>(window.layoutPassCount_),
        static_cast<unsigned long long>(window.workbenchTabState_.applyCount()),
        static_cast<unsigned long long>(window.workbenchTabState_.layoutRevision()),
        kLogIterations,
        static_cast<long long>(logElapsedMs.count()),
        static_cast<unsigned long long>(window.logFlushPassCount_),
        static_cast<unsigned long long>(trimRebuildCount));
    writeSelfTestTrace(message);

    DestroyWindow(window.window_);
    return ok;
}

} // namespace svm::win32

#endif
