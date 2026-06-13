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
#include "win32/win32_serial_port.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace svm::win32 {

enum class NativeLogKind {
    System,
    Tx,
    Rx,
    ModbusTx,
    ModbusRx,
    Error,
};

struct NativeLogEntry {
    NativeLogKind kind = NativeLogKind::System;
    std::wstring timestamp;
    std::wstring text;
    std::wstring payloadPrefix;
    std::vector<std::uint8_t> payload;
    bool hasPayload = false;
};

class NativeMainWindow final {
public:
    bool create(HINSTANCE instance);
    void show(int commandShow);
    int runMessageLoop();

    static bool runSelfTest();

private:
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
    void toggleConnection();
    void connectSerial();
    void disconnectSerial();
    void sendPayload();
    void pollSerial();
    void handleSerialFailure(const std::string& message);
    void tryAutoReconnect();
    void runModbusScan();
    void showAnalysisWorkspace();
    void showRuleVerification();
    void exportReport();
    void showAbout();
    void showDeferredFeature(const std::wstring& title, const std::wstring& message);
    void refreshCandidateCombo(const std::string& runId);
    std::optional<native_storage::MatchCandidateRecord> selectedCandidate() const;
    bool saveRuleFromCandidate(const native_storage::MatchCandidateRecord& candidate);
    bool runRuleVerification(const native_storage::ScanSessionRecord& session);
    void appendLog(const std::wstring& line);
    void appendLog(NativeLogKind kind, const std::wstring& line);
    void appendPayloadLog(NativeLogKind kind, const std::vector<std::uint8_t>& payload);
    void clearLog();
    std::size_t rebuildLogView();
    void appendVisibleLogEntry(const NativeLogEntry& entry);
    void appendVisibleLogText(NativeLogKind kind, const std::wstring& text);
    void addLogEntry(NativeLogEntry entry);
    std::wstring renderLogEntry(const NativeLogEntry& entry) const;
    bool logEntryMatchesFilter(const NativeLogEntry& entry) const;
    std::wstring visibleLogText() const;
    void updateLogFilter();
    void findNextLogMatch();
    void copyVisibleLogToClipboard();
    void exportVisibleLog();
    void setStatus(const std::wstring& text);
    void setSendModeStatus();
    std::wstring controlText(HWND control) const;
    void setControlText(HWND control, const std::wstring& text);
    std::vector<std::uint8_t> payloadFromInput(std::wstring* errorText) const;
    std::wstring formatPayloadForLog(const std::vector<std::uint8_t>& payload) const;
    unsigned int selectedTextCodePage() const;
    unsigned int selectedLogCodePage() const;
    std::string selectedPortName() const;
    SerialOpenOptions currentOpenOptions() const;
    void applySerialLineControl(WORD controlId);
    void updateRtsControlState();
    void applyLogTheme(int themeIndex);
    void updateLogTimestampMenu();
    std::filesystem::path defaultStoreDirectory() const;
    void saveRawEvent(std::string direction, const std::vector<std::uint8_t>& payload);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HMENU menu_ = nullptr;
    HWND connectionGroup_ = nullptr;
    HWND sendGroup_ = nullptr;
    HWND workflowGroup_ = nullptr;
    HWND logGroup_ = nullptr;
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
    HWND pauseScrollButton_ = nullptr;
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
    HFONT uiFont_ = nullptr;
    HMODULE richEditModule_ = nullptr;
    bool ownsUiFont_ = false;
    bool receiveLogUsesRichEdit_ = false;
    bool showLogTimestamps_ = true;
    int logThemeIndex_ = 0;
    std::deque<NativeLogEntry> logEntries_;
    std::wstring logFilterText_;
    std::wstring lastLogSearchText_;
    std::size_t lastLogSearchOffset_ = 0;
    std::size_t visibleLogChars_ = 0;
    std::size_t visibleLogLineCount_ = 0;
    bool uiPreferenceSaveFailureShown_ = false;
    Win32SerialPort serialPort_;
    native_storage::NativeSessionStore store_;
    std::vector<SerialPortDescriptor> availablePorts_;
    std::vector<native_storage::SendHistoryEntry> sendHistoryEntries_;
    std::string sessionId_ = "win32-native-session";
    std::optional<SerialOpenOptions> lastOpenOptions_;
    std::string reconnectPortName_;
    std::string latestMatchRunId_;
    std::string latestVerificationRunId_;
    bool waitingReconnect_ = false;
    bool scrollPaused_ = false;
    std::size_t hiddenLogLineCount_ = 0;
};

} // namespace svm::win32

#endif
