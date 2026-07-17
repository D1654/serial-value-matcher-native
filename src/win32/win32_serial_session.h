#pragma once

#include "transport/serial_session.h"
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
#include <optional>
#include <string>
#include <vector>

namespace svm::win32 {

class Win32SerialSessionTestAccess;

class Win32SerialSession final
    : public svm::transport::SerialSession,
      public svm::transport::SerialByteStream,
      public svm::transport::SerialWriteScheduler {
public:
    Win32SerialSession();
    ~Win32SerialSession() override;

    Win32SerialSession(const Win32SerialSession&) = delete;
    Win32SerialSession& operator=(const Win32SerialSession&) = delete;
    Win32SerialSession(Win32SerialSession&& other) noexcept = delete;
    Win32SerialSession& operator=(Win32SerialSession&& other) noexcept = delete;

    svm::transport::SerialOperationResult open(SerialOpenOptions options) override;
    svm::transport::SerialOperationResult close() override;
    svm::transport::SerialSessionSnapshot snapshot() const override;
    svm::transport::SerialOperationResult setDataTerminalReady(bool enabled) override;
    svm::transport::SerialOperationResult setRequestToSend(bool enabled) override;
    svm::transport::SerialByteStream& byteStream() noexcept override;
    svm::transport::SerialWriteScheduler& writeScheduler() noexcept override;
    svm::transport::SerialTerminalResult writeBytes(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline = {}) override;
    svm::transport::SerialReadResult readAvailable(
        std::size_t maxBytes,
        svm::transport::SerialDeadline deadline = {}) override;
    svm::transport::SerialWriteAdmissionResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline = {}) override;
    std::vector<svm::transport::SerialTerminalResult> cancelPendingWrites() override;
    std::vector<svm::transport::SerialTerminalResult> takeCompletedWrites() override;
    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override;

private:
    friend class Win32SerialSessionTestAccess;

    struct NativeIoOutcome {
        bool ok = false;
        std::size_t byteCount = 0;
        std::uint32_t nativeCode = 0;
        svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None;
    };

    struct ActiveWrite {
        svm::transport::SerialOperationId requestId = svm::transport::kUnassignedSerialOperationId;
        svm::transport::SerialSessionGeneration generation = svm::transport::kUnassignedSerialSessionGeneration;
        svm::transport::SerialDeadline deadline;
        std::size_t payloadBytes = 0;
        std::string endpoint;
        bool terminalPublished = false;
    };

    struct WriteAdmission {
        svm::transport::SerialWriteResult result;
        std::string endpoint;
        svm::transport::SerialOperationStatus status =
            svm::transport::SerialOperationStatus::RejectedInvalid;
        svm::transport::SerialErrorCategory category =
            svm::transport::SerialErrorCategory::InvalidInput;
        std::uint32_t nativeCode = 0;
    };

    struct WorkerStopOutcome {
        bool joined = true;
        bool threadHandleClosed = true;
        bool wakeEventHandleClosed = true;
        std::uint32_t nativeCode = 0;
    };

    svm::transport::SerialSessionSnapshot sessionSnapshotLocked() const;
    WriteAdmission enqueueWriteCore(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline);

    NativeIoOutcome writeBytesInternal(
        const std::uint8_t* payload,
        std::size_t size,
        svm::transport::SerialSessionGeneration expectedGeneration =
            svm::transport::kUnassignedSerialSessionGeneration,
        svm::transport::SerialDeadline deadline = {});
    NativeIoOutcome writeBytesToHandle(
        HANDLE handle,
        const std::uint8_t* payload,
        std::size_t size,
        svm::transport::SerialDeadline deadline,
        int readTimeoutMs,
        int writeTimeoutMs);
    HANDLE validatedHandleForGeneration(svm::transport::SerialSessionGeneration generation) const noexcept;
    svm::transport::SerialOperationResult operationResult(
        svm::transport::SerialOperationKind kind,
        svm::transport::SerialOperationStatus status,
        svm::transport::SerialSessionGeneration generation,
        std::string endpoint,
        svm::transport::SerialDeadline deadline = {},
        std::size_t byteCount = 0,
        svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None,
        std::uint32_t nativeCode = 0,
        svm::transport::SerialOperationId requestId = svm::transport::kUnassignedSerialOperationId);
    svm::transport::SerialOperationResult rejectedClosed(
        svm::transport::SerialOperationKind kind,
        const svm::transport::SerialSessionSnapshot& snapshot,
        svm::transport::SerialDeadline deadline = {});
    svm::transport::SerialTerminalResult terminalResult(
        const svm::transport::SerialWriteResult& result,
        svm::transport::SerialOperationStatus status,
        svm::transport::SerialErrorCategory category,
        std::string endpoint,
        std::uint32_t nativeCode = 0) const;
    void publishCompletion(svm::transport::SerialTerminalResult result);
    bool finalizeActiveWriteLocked(
        ActiveWrite request,
        svm::transport::SerialWriteResultStatus status,
        std::size_t byteCount,
        svm::transport::SerialErrorCategory category,
        std::uint32_t nativeCode);
    void publishInterruptedActiveWriteLocked(
        svm::transport::SerialErrorCategory category,
        std::uint32_t nativeCode);
    void settlePendingWritesLocked(
        svm::transport::SerialErrorCategory category,
        std::uint32_t nativeCode = 0);
    void markGenerationDisconnected(svm::transport::SerialSessionGeneration generation);
    static svm::transport::SerialErrorCategory nativeErrorCategory(
        std::uint32_t nativeCode,
        bool cancellationRequested) noexcept;
    static svm::transport::SerialOperationStatus operationStatus(
        svm::transport::SerialWriteResultStatus status) noexcept;
    static bool deadlineExpired(const svm::transport::SerialDeadline& deadline) noexcept;
    svm::transport::SerialOperationId allocateOperationId() noexcept;
    bool ensureWriteWorkerLocked(std::uint32_t& nativeCode);
    WorkerStopOutcome stopWriteWorker(
        svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::SessionClosed);
    void writeWorkerLoop();
    static DWORD WINAPI writeWorkerThreadProc(void* parameter);

    void* handle_ = nullptr;
    SerialOpenOptions options_;
    mutable CRITICAL_SECTION lifecycleLock_ = {};
    mutable CRITICAL_SECTION writeLock_ = {};
    mutable CRITICAL_SECTION ioLock_ = {};
    HANDLE writeWakeEvent_ = nullptr;
    HANDLE writeThread_ = nullptr;
    svm::transport::SerialWriteQueue writeQueue_;
    std::deque<svm::transport::SerialTerminalResult> completedWrites_;
    std::optional<ActiveWrite> activeWrite_;
    svm::transport::SerialSessionState state_ = svm::transport::SerialSessionState::Closed;
    svm::transport::SerialSessionGeneration generationCounter_ = svm::transport::kUnassignedSerialSessionGeneration;
    svm::transport::SerialSessionGeneration generation_ = svm::transport::kUnassignedSerialSessionGeneration;
    svm::transport::SerialOperationId nextOperationId_ = 1;
    bool writeWorkerStopRequested_ = false;
    svm::transport::SerialErrorCategory activeCancellationCategory_ =
        svm::transport::SerialErrorCategory::None;
};

} // namespace svm::win32

#endif
