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
    void createControls();
    void layoutControls(int width, int height);
    void setDefaultFonts();
    void refreshPorts();
    void connectSerial();
    void disconnectSerial();
    void sendPayload();
    void pollSerial();
    void appendLog(const std::wstring& line);
    void setStatus(const std::wstring& text);
    std::wstring controlText(HWND control) const;
    void setControlText(HWND control, const std::wstring& text);
    std::vector<std::uint8_t> payloadFromInput(std::wstring* errorText) const;
    std::filesystem::path defaultStoreDirectory() const;
    void saveRawEvent(std::string direction, const std::vector<std::uint8_t>& payload);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND portLabel_ = nullptr;
    HWND portCombo_ = nullptr;
    HWND refreshButton_ = nullptr;
    HWND baudLabel_ = nullptr;
    HWND baudEdit_ = nullptr;
    HWND connectButton_ = nullptr;
    HWND disconnectButton_ = nullptr;
    HWND hexCheck_ = nullptr;
    HWND sendEdit_ = nullptr;
    HWND sendButton_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND receiveLog_ = nullptr;
    HWND statusText_ = nullptr;
    HFONT uiFont_ = nullptr;
    Win32SerialPort serialPort_;
    native_storage::NativeSessionStore store_;
    std::string sessionId_ = "win32-native-session";
};

} // namespace svm::win32

#endif
