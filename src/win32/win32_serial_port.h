#pragma once

#include "transport/serial_transport.h"
#include "win32/win32_serial_types.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace svm::win32 {

class Win32SerialPort final : public svm::transport::SerialTransport {
public:
    Win32SerialPort();
    ~Win32SerialPort() override;

    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;
    Win32SerialPort(Win32SerialPort&& other) noexcept = delete;
    Win32SerialPort& operator=(Win32SerialPort&& other) noexcept = delete;

    bool open(SerialOpenOptions options) override;
    void close() override;
    bool isOpen() const noexcept override;

    std::string endpoint() const override;
    std::string lastErrorText() const override;
    bool usesHardwareRtsCts() const noexcept override;

    bool setDataTerminalReady(bool enabled) override;
    bool setRequestToSend(bool enabled) override;
    SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload) override;
    SerialIoResult writeBytes(const std::uint8_t* payload, std::size_t size);
    svm::transport::SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) override;
    std::vector<svm::transport::SerialWriteResult> cancelPendingWrites() override;
    std::vector<svm::transport::SerialWriteResult> takeCompletedWrites() override;
    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override;
    bool waitForReadyRead(int timeoutMs) override;
    std::vector<std::uint8_t> readAvailable(std::size_t maxBytes) override;

private:
    SerialIoResult writeBytesInternal(const std::uint8_t* payload, std::size_t size, bool updateLastError);
    bool ensureWriteWorkerLocked();
    void stopWriteWorker();
    void writeWorkerLoop();
    static DWORD WINAPI writeWorkerThreadProc(void* parameter);

    void* handle_ = nullptr;
    SerialOpenOptions options_;
    std::string lastErrorText_;
    mutable CRITICAL_SECTION writeLock_ = {};
    HANDLE writeWakeEvent_ = nullptr;
    HANDLE writeThread_ = nullptr;
    svm::transport::SerialWriteQueue writeQueue_;
    std::deque<svm::transport::SerialWriteResult> completedWrites_;
    bool writeWorkerStopRequested_ = false;
    bool writeInProgress_ = false;
};

} // namespace svm::win32

#endif
