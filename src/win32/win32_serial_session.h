#pragma once

#include "transport/serial_session.h"
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

class Win32SerialSessionTestAccess;

// Temporary bridge while in-repo callers migrate to the narrow session capabilities.
class Win32SerialSession final : public svm::transport::SerialTransport {
public:
    Win32SerialSession();
    ~Win32SerialSession() override;

    Win32SerialSession(const Win32SerialSession&) = delete;
    Win32SerialSession& operator=(const Win32SerialSession&) = delete;
    Win32SerialSession(Win32SerialSession&& other) noexcept = delete;
    Win32SerialSession& operator=(Win32SerialSession&& other) noexcept = delete;

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

    svm::transport::SerialSession& sessionCapability() noexcept;

private:
    friend class Win32SerialSessionTestAccess;

    class CapabilityView final
        : public svm::transport::SerialSession,
          public svm::transport::SerialByteStream,
          public svm::transport::SerialWriteScheduler {
    public:
        explicit CapabilityView(Win32SerialSession& owner) noexcept;

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
        Win32SerialSession& owner_;
    };

    struct NativeIoOutcome {
        bool ok = false;
        std::size_t byteCount = 0;
        std::uint32_t nativeCode = 0;
        svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None;
        std::string diagnostic;
    };

    struct CompletedWrite {
        svm::transport::SerialTerminalResult result;
        std::string diagnostic;
    };

    struct ActiveWrite {
        svm::transport::SerialOperationId requestId = svm::transport::kUnassignedSerialOperationId;
        svm::transport::SerialSessionGeneration generation = svm::transport::kUnassignedSerialSessionGeneration;
        svm::transport::SerialDeadline deadline;
        std::size_t payloadBytes = 0;
        std::string endpoint;
    };

    struct WriteAdmission {
        svm::transport::SerialWriteResult result;
        std::string endpoint;
        svm::transport::SerialOperationStatus status =
            svm::transport::SerialOperationStatus::RejectedInvalid;
        svm::transport::SerialErrorCategory category =
            svm::transport::SerialErrorCategory::InvalidInput;
    };

    svm::transport::SerialOperationResult openOperation(SerialOpenOptions options);
    svm::transport::SerialOperationResult closeOperation();
    svm::transport::SerialSessionSnapshot sessionSnapshot() const;
    svm::transport::SerialSessionSnapshot sessionSnapshotLocked() const;
    svm::transport::SerialOperationResult setDataTerminalReadyOperation(bool enabled);
    svm::transport::SerialOperationResult setRequestToSendOperation(bool enabled);
    svm::transport::SerialTerminalResult writeOperation(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline);
    svm::transport::SerialReadResult readOperation(
        std::size_t maxBytes,
        svm::transport::SerialDeadline deadline);
    svm::transport::SerialWriteAdmissionResult enqueueOperation(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline);
    WriteAdmission enqueueWriteCore(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs,
        svm::transport::SerialDeadline deadline);
    std::vector<svm::transport::SerialTerminalResult> cancelPendingOperations();
    std::vector<svm::transport::SerialTerminalResult> takeCompletedOperations();

    NativeIoOutcome writeBytesInternal(
        const std::uint8_t* payload,
        std::size_t size,
        bool updateLastError,
        svm::transport::SerialSessionGeneration expectedGeneration =
            svm::transport::kUnassignedSerialSessionGeneration);
    NativeIoOutcome writeBytesToHandle(HANDLE handle, const std::uint8_t* payload, std::size_t size);
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
    void publishCompletion(
        svm::transport::SerialTerminalResult result,
        std::string diagnostic = {});
    bool completeActiveWrite(
        const ActiveWrite& request,
        svm::transport::SerialWriteResultStatus status,
        std::size_t byteCount,
        svm::transport::SerialErrorCategory category,
        std::uint32_t nativeCode,
        std::string diagnostic);
    void settlePendingWritesLocked(svm::transport::SerialErrorCategory category);
    void markGenerationDisconnected(
        svm::transport::SerialSessionGeneration generation,
        const std::string& diagnostic);
    static svm::transport::SerialErrorCategory nativeErrorCategory(
        std::uint32_t nativeCode,
        bool cancellationRequested) noexcept;
    static svm::transport::SerialOperationStatus operationStatus(
        svm::transport::SerialWriteResultStatus status) noexcept;
    static svm::transport::SerialWriteResult legacyResult(const CompletedWrite& completed);
    static bool deadlineExpired(const svm::transport::SerialDeadline& deadline) noexcept;
    svm::transport::SerialOperationId allocateOperationId() noexcept;
    bool ensureWriteWorkerLocked();
    void stopWriteWorker(
        svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::SessionClosed);
    void writeWorkerLoop();
    static DWORD WINAPI writeWorkerThreadProc(void* parameter);

    CapabilityView capabilityView_;
    void* handle_ = nullptr;
    SerialOpenOptions options_;
    std::string lastErrorText_;
    mutable CRITICAL_SECTION lifecycleLock_ = {};
    mutable CRITICAL_SECTION writeLock_ = {};
    mutable CRITICAL_SECTION ioLock_ = {};
    HANDLE writeWakeEvent_ = nullptr;
    HANDLE writeThread_ = nullptr;
    svm::transport::SerialWriteQueue writeQueue_;
    std::deque<CompletedWrite> completedWrites_;
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
