#pragma once

#include "transport/serial_write_queue.h"
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
#include <string>
#include <vector>

namespace svm::win32 {

class Win32SerialPort final {
public:
    Win32SerialPort();
    ~Win32SerialPort();

    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;
    Win32SerialPort(Win32SerialPort&& other) noexcept = delete;
    Win32SerialPort& operator=(Win32SerialPort&& other) noexcept = delete;

    bool open(SerialOpenOptions options);
    void close();
    bool isOpen() const noexcept;

    std::string endpoint() const;
    std::string lastErrorText() const;
    bool usesHardwareRtsCts() const noexcept;

    bool setDataTerminalReady(bool enabled);
    bool setRequestToSend(bool enabled);
    SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload);
    SerialIoResult writeBytes(const std::uint8_t* payload, std::size_t size);
    svm::transport::SerialWriteResult enqueueWrite(std::vector<std::uint8_t> payload);
    std::vector<svm::transport::SerialWriteResult> cancelPendingWrites();
    std::vector<svm::transport::SerialWriteResult> takeCompletedWrites();
    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const;
    bool waitForReadyRead(int timeoutMs);
    std::vector<std::uint8_t> readAvailable(std::size_t maxBytes);

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
