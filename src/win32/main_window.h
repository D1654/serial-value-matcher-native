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
    void setStatus(const std::wstring& text);
    std::wstring controlText(HWND control) const;
    void setControlText(HWND control, const std::wstring& text);
    std::vector<std::uint8_t> payloadFromInput(std::wstring* errorText) const;
    SerialOpenOptions currentOpenOptions() const;
    std::filesystem::path defaultStoreDirectory() const;
    void saveRawEvent(std::string direction, const std::vector<std::uint8_t>& payload);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HMENU menu_ = nullptr;
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
    HWND sendModeCombo_ = nullptr;
    HWND lineEndingCombo_ = nullptr;
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
    HWND scanFunctionCombo_ = nullptr;
    HWND scanStartEdit_ = nullptr;
    HWND scanEndEdit_ = nullptr;
    HWND targetStatic_ = nullptr;
    HWND targetLabelEdit_ = nullptr;
    HWND targetValueEdit_ = nullptr;
    HWND targetUnitEdit_ = nullptr;
    HWND toleranceStatic_ = nullptr;
    HWND toleranceEdit_ = nullptr;
    HWND candidateCombo_ = nullptr;
    HWND receiveLog_ = nullptr;
    HWND statusText_ = nullptr;
    HFONT uiFont_ = nullptr;
    bool ownsUiFont_ = false;
    Win32SerialPort serialPort_;
    native_storage::NativeSessionStore store_;
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
