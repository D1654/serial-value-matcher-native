#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "native_storage/native_session_store.h"
#include "win32/native_file_send_state.h"
#include "win32/native_log_model.h"
#include "win32/native_modbus_scan_worker.h"
#include "win32/native_serial_io_state.h"
#include "win32/native_ui_preferences.h"
#include "win32/native_send_history_state.h"
#include "win32/win32_serial_port.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace svm::win32 {

class NativeMainWindow final {
public:
    bool create(HINSTANCE instance);
    void show(int commandShow);
    int runMessageLoop();

    static bool runSelfTest();
    static bool runUiPerformanceTest();

private:
    struct WorkbenchVisibility {
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

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void createMenus();
    void createControls();
    void populateSerialOptionControls();
    void layoutControls(int width, int height);
    void setDefaultFonts();
    void refreshPorts();
    void refreshSendHistory();
    void applySelectedHistory();
    void applyLatestSerialProfile();
    void saveCurrentSerialProfile();
    void applyUiPreferences();
    void saveUiPreferences();
    void updateWorkbenchTab();
    void applyWorkbenchTabVisibility(int tabIndex);
    void toggleConnection();
    void connectSerial();
    void disconnectSerial();
    void closeSerialPort(const std::wstring& statusText);
    std::wstring serialIoBusyStatus() const;
    void releaseModbusScanOwnership();
    void sendPayload();
    bool sendPayloadFromText(const std::wstring& text, bool saveHistory);
    void sendQuickPayload(std::size_t index);
    void updateTimedSendTimer();
    void browseFileSend();
    void startFileSend();
    void stopFileSend(const std::wstring& statusText = {});
    void pumpFileSend();
    void updateFileSendProgress();
    void pollSerial();
    void handleSerialFailure(const std::string& message);
    void tryAutoReconnect();
    void runModbusScan();
    void requestCancelModbusScan();
    void handleModbusScanProgress(NativeModbusScanProgress* progress);
    void handleModbusScanDataBatch(NativeModbusScanDataBatch* batch);
    void handleModbusScanDone(NativeModbusScanResult* result);
    void joinFinishedModbusScanThread();
    void updateCompletedModbusScanProgress(const NativeModbusScanResult& result);
    std::wstring persistCompletedModbusScan(const NativeModbusScanResult& result);
    bool handleCompletedModbusScanDisconnect(const NativeModbusScanResult& result, bool shouldDisconnectAfterScan);
    void setModbusScanRunningUi(bool running);
    void updateModbusScanProgress(
        std::size_t completedBlocks,
        std::size_t totalBlocks,
        std::size_t successBlocks,
        std::size_t failedBlocks,
        std::size_t observations);
    void showAnalysisWorkspace();
    void showRuleVerification();
    void exportReport();
    void showAbout();
    void showDeferredFeature(const std::wstring& title, const std::wstring& message);
    void refreshCandidateCombo(const std::string& runId);
    bool loadCandidateCache(const std::string& runId);
    std::optional<native_storage::MatchCandidateRecord> selectedCandidate();
    bool saveRuleFromCandidate(const native_storage::MatchCandidateRecord& candidate);
    bool runRuleVerification(const native_storage::ScanSessionRecord& session);
    void appendLog(const std::wstring& line);
    void appendLog(NativeLogKind kind, const std::wstring& line);
    void appendPayloadLog(NativeLogKind kind, const std::vector<std::uint8_t>& payload);
    void clearLog();
    std::size_t rebuildLogView();
    void queueVisibleLogEntry(const NativeLogEntry& entry);
    void queueVisibleLogText(NativeLogKind kind, std::wstring text);
    void scheduleLogFlush();
    void flushPendingLogEntries();
    void appendVisibleLogEntry(const NativeLogEntry& entry);
    void appendVisibleLogText(NativeLogKind kind, const std::wstring& text);
    void insertVisibleLogText(NativeLogKind kind, const std::wstring& text);
    void addLogEntry(NativeLogEntry entry);
    std::wstring renderLogEntry(const NativeLogEntry& entry) const;
    bool logEntryMatchesFilter(const NativeLogEntry& entry) const;
    std::wstring visibleLogText() const;
    void updateLogFilter();
    void findNextLogMatch();
    void copyVisibleLogToClipboard();
    void exportVisibleLog();
    void followLatestLog();
    void setStatus(const std::wstring& text);
    void updateStatusSegments();
    void setSendModeStatus();
    std::wstring controlText(HWND control) const;
    std::wstring analysisInputText(HWND control) const;
    void setControlText(HWND control, const std::wstring& text);
    std::vector<std::uint8_t> payloadFromInput(std::wstring* errorText) const;
    std::vector<std::uint8_t> payloadFromText(const std::wstring& text, std::wstring* errorText) const;
    std::wstring formatPayloadForLog(const std::vector<std::uint8_t>& payload) const;
    unsigned int selectedTextCodePage() const;
    unsigned int selectedLogCodePage() const;
    std::string selectedPortName() const;
    SerialOpenOptions currentOpenOptions() const;
    void applySerialLineControl(WORD controlId);
    void updateRtsControlState();
    void applyLogTheme(int themeIndex);
    void applyLogCacheLimit(std::size_t visibleCharLimit);
    void applyRawEventRetentionLimit(int retentionLimitMb);
    void updateLogTimestampMenu();
    void hideWorkbenchTabControls();
    void setWorkbenchTabControlsVisible(int tabIndex, bool visible);
    void updateSideHelp(int tabIndex);
    std::filesystem::path defaultStoreDirectory() const;
    void saveRawEvent(std::string direction, const std::vector<std::uint8_t>& payload);
    bool saveRawEvents(std::vector<native_storage::RawIoEvent> events);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HMENU menu_ = nullptr;
    HWND connectionGroup_ = nullptr;
    HWND sendGroup_ = nullptr;
    HWND workflowGroup_ = nullptr;
    HWND logGroup_ = nullptr;
    HWND serialPanelTitle_ = nullptr;
    HWND logPanelTitle_ = nullptr;
    HWND workPanelTitle_ = nullptr;
    HWND workTabs_ = nullptr;
    HWND workPageBackground_ = nullptr;
    HWND workflowHint_ = nullptr;
    HWND portLabel_ = nullptr;
    HWND portCombo_ = nullptr;
    HWND refreshButton_ = nullptr;
    HWND saveProfileButton_ = nullptr;
    HWND baudLabel_ = nullptr;
    HWND baudCombo_ = nullptr;
    HWND dataBitsLabel_ = nullptr;
    HWND dataBitsCombo_ = nullptr;
    HWND parityLabel_ = nullptr;
    HWND parityCombo_ = nullptr;
    HWND stopBitsLabel_ = nullptr;
    HWND stopBitsCombo_ = nullptr;
    HWND flowControlLabel_ = nullptr;
    HWND flowControlCombo_ = nullptr;
    HWND dtrCheck_ = nullptr;
    HWND rtsCheck_ = nullptr;
    HWND autoReconnectCheck_ = nullptr;
    HWND connectButton_ = nullptr;
    HWND disconnectButton_ = nullptr;
    HWND sendModeLabel_ = nullptr;
    HWND sendModeCombo_ = nullptr;
    HWND sendEncodingLabel_ = nullptr;
    HWND textEncodingCombo_ = nullptr;
    HWND lineEndingLabel_ = nullptr;
    HWND lineEndingCombo_ = nullptr;
    HWND sendHistoryLabel_ = nullptr;
    HWND logFormatLabel_ = nullptr;
    HWND logFormatCombo_ = nullptr;
    HWND logEncodingLabel_ = nullptr;
    HWND logEncodingCombo_ = nullptr;
    HWND logFilterLabel_ = nullptr;
    HWND logFilterEdit_ = nullptr;
    HWND logSearchLabel_ = nullptr;
    HWND logSearchEdit_ = nullptr;
    HWND findLogButton_ = nullptr;
    HWND copyLogButton_ = nullptr;
    HWND exportLogButton_ = nullptr;
    HWND historyCombo_ = nullptr;
    HWND sendEdit_ = nullptr;
    HWND sendButton_ = nullptr;
    HWND timedSendCheck_ = nullptr;
    HWND timedPeriodLabel_ = nullptr;
    HWND timedPeriodEdit_ = nullptr;
    std::array<HWND, 10> quickSendEdits_ = {};
    std::array<HWND, 10> quickSendButtons_ = {};
    HWND filePathLabel_ = nullptr;
    HWND filePathEdit_ = nullptr;
    HWND fileBrowseButton_ = nullptr;
    HWND fileSendButton_ = nullptr;
    HWND fileStopButton_ = nullptr;
    HWND fileDelayLabel_ = nullptr;
    HWND fileDelayCombo_ = nullptr;
    HWND fileProgressLabel_ = nullptr;
    HWND fileProgress_ = nullptr;
    HWND logCacheLabel_ = nullptr;
    HWND logCacheCombo_ = nullptr;
    HWND rawEventRetentionLabel_ = nullptr;
    HWND rawEventRetentionCombo_ = nullptr;
    HWND sideActionSeparator_ = nullptr;
    HWND pauseScrollButton_ = nullptr;
    HWND sideHelpSeparator_ = nullptr;
    HWND sideHelpFrame_ = nullptr;
    HWND sideHelpTitle_ = nullptr;
    HWND sideHelpText_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND modbusButton_ = nullptr;
    HWND analysisButton_ = nullptr;
    HWND ruleVerifyButton_ = nullptr;
    HWND exportReportButton_ = nullptr;
    HWND scanSlaveEdit_ = nullptr;
    HWND scanSlaveLabel_ = nullptr;
    HWND scanFunctionCombo_ = nullptr;
    HWND scanFunctionLabel_ = nullptr;
    HWND scanStartEdit_ = nullptr;
    HWND scanStartLabel_ = nullptr;
    HWND scanEndEdit_ = nullptr;
    HWND scanEndLabel_ = nullptr;
    HWND modbusProgressLabel_ = nullptr;
    HWND modbusProgress_ = nullptr;
    HWND modbusProgressText_ = nullptr;
    HWND scanSectionLabel_ = nullptr;
    HWND analysisSectionLabel_ = nullptr;
    HWND targetStatic_ = nullptr;
    HWND targetLabelEdit_ = nullptr;
    HWND targetValueStatic_ = nullptr;
    HWND targetValueEdit_ = nullptr;
    HWND targetUnitStatic_ = nullptr;
    HWND targetUnitEdit_ = nullptr;
    HWND toleranceStatic_ = nullptr;
    HWND toleranceEdit_ = nullptr;
    HWND candidateStatic_ = nullptr;
    HWND candidateCombo_ = nullptr;
    HWND receiveLog_ = nullptr;
    HWND statusText_ = nullptr;
    HWND txStatusText_ = nullptr;
    HWND rxStatusText_ = nullptr;
    HWND clockStatusText_ = nullptr;
    HFONT uiFont_ = nullptr;
    HMODULE richEditModule_ = nullptr;
    bool ownsUiFont_ = false;
    bool receiveLogUsesRichEdit_ = false;
    bool showLogTimestamps_ = true;
    WorkbenchVisibility workbenchVisibility_;
    bool workbenchVisibilityReady_ = false;
    RECT workbenchRedrawRect_ = {};
    int lastSideHelpTabIndex_ = -1;
    int activeWorkbenchTabIndex_ = -1;
    std::uint64_t layoutPassCount_ = 0;
    std::uint64_t workbenchLayoutRevision_ = 0;
    std::uint64_t workbenchAppliedRevision_ = 0;
    std::uint64_t workbenchTabApplyCount_ = 0;
    int logThemeIndex_ = 0;
    std::size_t logVisibleCharLimit_ = kNativeDefaultLogVisibleChars;
    std::size_t logEntryLimit_ = 2000;
    std::deque<NativeLogEntry> logEntries_;
    NativeLogFilterState logFilterState_;
    std::size_t visibleLogChars_ = 0;
    std::size_t visibleLogLineCount_ = 0;
    std::deque<NativePendingLogLine> pendingLogLines_;
    std::size_t pendingLogChars_ = 0;
    std::size_t logTrimmedSinceRebuild_ = 0;
    bool logFlushTimerActive_ = false;
    std::uint64_t logFlushPassCount_ = 0;
    std::uint64_t logRebuildPassCount_ = 0;
    std::uint64_t logQueuedLineCount_ = 0;
    bool uiPreferenceSaveFailureShown_ = false;
    Win32SerialPort serialPort_;
    NativeSerialIoState serialIoState_;
    native_storage::NativeSessionStore store_;
    std::vector<SerialPortDescriptor> availablePorts_;
    NativeSendHistoryState sendHistoryState_;
    std::vector<native_storage::MatchCandidateRecord> candidateRecords_;
    std::string sessionId_ = "win32-native-session";
    std::optional<SerialOpenOptions> lastOpenOptions_;
    std::string reconnectPortName_;
    std::string latestMatchRunId_;
    std::string cachedCandidateRunId_;
    std::string latestVerificationRunId_;
    bool waitingReconnect_ = false;
    bool scrollPaused_ = false;
    bool logAutoFollow_ = true;
    bool logHistoryReadNoticeShown_ = false;
    std::size_t hiddenLogLineCount_ = 0;
    std::uint64_t txByteCount_ = 0;
    std::uint64_t rxByteCount_ = 0;
    bool timedSendActive_ = false;
    NativeFileSendState fileSend_;
    HANDLE modbusScanThread_ = nullptr;
    std::atomic_bool modbusScanCancelRequested_ = false;
    std::atomic_bool modbusScanRunning_ = false;
    bool disconnectAfterModbusScan_ = false;
};

} // namespace svm::win32

#endif
