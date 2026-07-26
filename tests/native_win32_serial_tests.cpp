#include "win32/win32_serial_types.h"
#if defined(_WIN32)
#include "win32/win32_serial_session.h"
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <concepts>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
namespace svm::win32 {

class Win32SerialSessionTestAccess final {
public:
    static bool installExitedWorker(Win32SerialSession& session) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        HANDLE thread = CreateThread(nullptr, 0, &completedThreadProc, nullptr, 0, nullptr);
        if (thread == nullptr) {
            CloseHandle(event);
            return false;
        }
        if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
            CloseHandle(thread);
            CloseHandle(event);
            return false;
        }

        EnterCriticalSection(&session.writeLock_);
        session.writeWakeEvent_ = event;
        session.writeThread_ = thread;
        LeaveCriticalSection(&session.writeLock_);
        return true;
    }

    static void publishSyntheticOpen(
        Win32SerialSession& session,
        svm::transport::SerialSessionGeneration generation,
        std::string endpoint) {
        EnterCriticalSection(&session.writeLock_);
        const bool generationStarted = session.writeQueue_.beginGeneration(generation);
        assert(generationStarted);
        session.state_ = svm::transport::SerialSessionState::Open;
        session.generation_ = generation;
        session.generationCounter_ = std::max(session.generationCounter_, generation);
        session.options_.portName = std::move(endpoint);
        LeaveCriticalSection(&session.writeLock_);
    }

    static svm::transport::SerialWriteResult enqueueWithoutWorker(
        Win32SerialSession& session,
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline) {
        EnterCriticalSection(&session.writeLock_);
        svm::transport::SerialWriteResult result = session.writeQueue_.enqueue(
            std::move(payload),
            svm::transport::kDefaultSerialWriteTimeoutMs,
            session.generation_,
            deadline);
        LeaveCriticalSection(&session.writeLock_);
        return result;
    }

    static std::optional<svm::transport::SerialWriteRequest> activateWithoutWorker(Win32SerialSession& session) {
        EnterCriticalSection(&session.writeLock_);
        std::optional<svm::transport::SerialWriteRequest> request = session.writeQueue_.activateNext();
        if (request.has_value()) {
            session.activeWrite_ = Win32SerialSession::ActiveWrite{
                .requestId = request->id,
                .generation = request->generation,
                .deadline = request->deadline,
                .payloadBytes = request->payloadBytes,
                .endpoint = session.options_.portName,
            };
        }
        LeaveCriticalSection(&session.writeLock_);
        return request;
    }

    static bool completeActive(
        Win32SerialSession& session,
        svm::transport::SerialOperationId requestId,
        svm::transport::SerialSessionGeneration generation,
        svm::transport::SerialWriteResultStatus status,
        std::size_t byteCount,
        svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None,
        std::uint32_t nativeCode = 0) {
        EnterCriticalSection(&session.writeLock_);
        if (!session.activeWrite_.has_value()) {
            LeaveCriticalSection(&session.writeLock_);
            return false;
        }
        Win32SerialSession::ActiveWrite completion = session.activeWrite_.value();
        completion.requestId = requestId;
        completion.generation = generation;
        const bool completed = session.finalizeActiveWriteLocked(
            completion,
            status,
            byteCount,
            category,
            nativeCode);
        LeaveCriticalSection(&session.writeLock_);
        return completed;
    }

    static bool installBlockedWorker(Win32SerialSession& session, HANDLE releaseEvent) {
        HANDLE wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (wakeEvent == nullptr) {
            return false;
        }
        HANDLE thread = CreateThread(nullptr, 0, &blockedThreadProc, releaseEvent, 0, nullptr);
        if (thread == nullptr) {
            CloseHandle(wakeEvent);
            return false;
        }

        EnterCriticalSection(&session.writeLock_);
        session.writeWakeEvent_ = wakeEvent;
        session.writeThread_ = thread;
        LeaveCriticalSection(&session.writeLock_);
        return true;
    }

    static bool hasWorkerResources(Win32SerialSession& session) {
        EnterCriticalSection(&session.writeLock_);
        const bool present = session.writeThread_ != nullptr && session.writeWakeEvent_ != nullptr;
        LeaveCriticalSection(&session.writeLock_);
        return present;
    }

    static svm::transport::SerialErrorCategory classifyNativeError(
        std::uint32_t nativeCode,
        bool cancellationRequested) noexcept {
        return Win32SerialSession::nativeErrorCategory(nativeCode, cancellationRequested);
    }

    static void markDisconnected(
        Win32SerialSession& session,
        svm::transport::SerialSessionGeneration generation) {
        session.markGenerationDisconnected(generation);
    }

    static svm::transport::SerialOperationResult rejectFromSnapshot(
        Win32SerialSession& session,
        const svm::transport::SerialSessionSnapshot& snapshot) {
        return session.rejectedClosed(svm::transport::SerialOperationKind::Read, snapshot);
    }

private:
    static DWORD WINAPI completedThreadProc(void*) {
        return 0;
    }

    static DWORD WINAPI blockedThreadProc(void* parameter) {
        return WaitForSingleObject(static_cast<HANDLE>(parameter), INFINITE) == WAIT_OBJECT_0
            ? 0
            : 1;
    }
};

} // namespace svm::win32
#endif

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void validatesSerialOpenOptions() {
    svm::win32::SerialOpenOptions options;
    options.portName = "COM3";
    assert(svm::win32::validateSerialOpenOptions(options).ok);

    options.portName = "";
    const auto missingPort = svm::win32::validateSerialOpenOptions(options);
    assert(!missingPort.ok);
    assert(contains(missingPort.errorMessage, "未选择串口"));

    options.portName = "ttyUSB0";
    const auto badPort = svm::win32::validateSerialOpenOptions(options);
    assert(!badPort.ok);
    assert(contains(badPort.errorMessage, "COM1"));

    options.portName = R"(\\.\COM12)";
    assert(svm::win32::validateSerialOpenOptions(options).ok);

    for (const std::string& unsafePort : {
             R"(\\.\PhysicalDrive0)",
             R"(\\?\PhysicalDrive0)",
             R"(\\.\pipe\serial-test)",
             R"(\\?\GLOBALROOT\Device\HarddiskVolume1)",
             "COM0",
             "COM01",
             "COM1suffix",
         }) {
        options.portName = unsafePort;
        assert(!svm::win32::validateSerialOpenOptions(options).ok);
    }

    options.portName = R"(\\?\com12)";
    assert(svm::win32::validateSerialOpenOptions(options).ok);

    options.baudRate = 0;
    const auto badBaud = svm::win32::validateSerialOpenOptions(options);
    assert(!badBaud.ok);
    assert(contains(badBaud.errorMessage, "波特率"));

    options.baudRate = 115200;
    options.dataBits = 9;
    const auto badDataBits = svm::win32::validateSerialOpenOptions(options);
    assert(!badDataBits.ok);
    assert(contains(badDataBits.errorMessage, "数据位"));

    options.dataBits = 8;
    options.stopBits = svm::win32::SerialStopBits::OnePointFive;
    const auto badOnePointFiveStopBits = svm::win32::validateSerialOpenOptions(options);
    assert(!badOnePointFiveStopBits.ok);
    assert(contains(badOnePointFiveStopBits.errorMessage, "1.5"));

    options.dataBits = 5;
    options.stopBits = svm::win32::SerialStopBits::Two;
    const auto badFiveDataBitsTwoStopBits = svm::win32::validateSerialOpenOptions(options);
    assert(!badFiveDataBitsTwoStopBits.ok);
    assert(contains(badFiveDataBitsTwoStopBits.errorMessage, "5 数据位"));

    options.stopBits = svm::win32::SerialStopBits::OnePointFive;
    assert(svm::win32::validateSerialOpenOptions(options).ok);

    options.dataBits = 8;
    options.stopBits = svm::win32::SerialStopBits::One;
    options.readTimeoutMs = -1;
    const auto badTimeout = svm::win32::validateSerialOpenOptions(options);
    assert(!badTimeout.ok);
    assert(contains(badTimeout.errorMessage, "超时"));

    options.readTimeoutMs = 1000;
    options.writeTimeoutMs = 0;
    const auto zeroWriteTimeout = svm::win32::validateSerialOpenOptions(options);
    assert(!zeroWriteTimeout.ok);
    assert(contains(zeroWriteTimeout.errorMessage, "超时"));

    options.writeTimeoutMs = svm::transport::kSerialTerminalResultTargetMs + 1;
    const auto excessiveWriteTimeout = svm::win32::validateSerialOpenOptions(options);
    assert(!excessiveWriteTimeout.ok);
    assert(contains(excessiveWriteTimeout.errorMessage, "超时"));

    options.writeTimeoutMs = svm::transport::kSerialTerminalResultTargetMs;
    assert(svm::win32::validateSerialOpenOptions(options).ok);
    options.readBufferSize = 0;
    const auto badBuffer = svm::win32::validateSerialOpenOptions(options);
    assert(!badBuffer.ok);
    assert(contains(badBuffer.errorMessage, "缓冲区"));
}

void normalizesComPortNamesAndDevicePaths() {
    assert(svm::win32::trimPortName("  COM10 \t") == "COM10");
    assert(svm::win32::normalizedComPortName(" com3 ") == "COM3");
    assert(svm::win32::normalizedComPortName(R"(\\.\com42)") == "COM42");
    assert(svm::win32::normalizedComPortName(R"(\\?\com42)") == "COM42");
    assert(svm::win32::comPortNumber("COM1") == 1);
    assert(svm::win32::comPortNumber(R"(\\.\COM256)") == 256);
    assert(svm::win32::comPortNumber("COM0") == -1);
    assert(svm::win32::comPortNumber("COM01") == -1);
    assert(svm::win32::comPortNumber("COM") == -1);

    assert(svm::win32::isLikelyComPortName("COM12"));
    assert(!svm::win32::isLikelyComPortName("ttyS0"));
    assert(svm::win32::makeWin32DevicePath("COM10") == R"(\\.\COM10)");
    assert(svm::win32::makeWin32DevicePath(R"(\\?\com10)") == R"(\\.\COM10)");
    assert(svm::win32::makeWin32DevicePath(R"(\\.\PhysicalDrive0)").empty());
    assert(svm::win32::makeWin32DevicePath(R"(\\.\pipe\serial-test)").empty());
    assert(svm::win32::stripWin32DevicePrefix(R"(\\.\COM10)") == "COM10");
}

void keepsChineseNamesForSettings() {
    assert(svm::win32::serialParityName(svm::win32::SerialParity::None) == "无校验");
    assert(svm::win32::serialParityName(svm::win32::SerialParity::Even) == "偶校验");
    assert(svm::win32::serialStopBitsName(svm::win32::SerialStopBits::Two) == "2 位停止位");
    assert(svm::win32::serialFlowControlName(svm::win32::SerialFlowControl::HardwareRtsCts) == "RTS/CTS 硬件流控");
}

void translatesCommonWin32ErrorsToActionableChinese() {
    const std::string missing = svm::win32::win32SerialErrorText(2, "打开串口");
    assert(contains(missing, "打开串口失败"));
    assert(contains(missing, "没有找到"));

    const std::string denied = svm::win32::win32SerialErrorText(5, "打开串口");
    assert(contains(denied, "串口被占用或权限不足"));

    const std::string sharing = svm::win32::win32SerialErrorText(32, "打开串口");
    assert(contains(sharing, "关闭其他串口助手"));

    const std::string timeout = svm::win32::win32SerialErrorText(121, "读取串口");
    assert(contains(timeout, "操作超时"));

    const std::string unknown = svm::win32::win32SerialErrorText(123456, "写入串口");
    assert(contains(unknown, "未知串口错误"));
    assert(contains(unknown, "123456"));
}

#if defined(_WIN32)
svm::transport::SerialDeadline deadlineAfter(int milliseconds) {
    return {
        .expiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds),
    };
}

void closedPortAsyncWriteFailsWithoutQueueing() {
    svm::win32::Win32SerialSession session;
    static_assert(std::derived_from<svm::win32::Win32SerialSession, svm::transport::SerialSession>);
    static_assert(std::derived_from<svm::win32::Win32SerialSession, svm::transport::SerialByteStream>);
    static_assert(std::derived_from<svm::win32::Win32SerialSession, svm::transport::SerialWriteScheduler>);
    static_assert(!std::copy_constructible<svm::win32::Win32SerialSession>);
    static_assert(!std::movable<svm::win32::Win32SerialSession>);
    svm::transport::SerialWriteScheduler& scheduler = session;
    const auto result = scheduler.enqueueWrite({0x01}, deadlineAfter(100));
    assert(result.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(result.error.category == svm::transport::SerialErrorCategory::SessionClosed);
    assert(!result.operation.assigned());

    const auto snapshot = scheduler.writeQueueSnapshot();
    assert(snapshot.pendingCount == 0);
    assert(scheduler.takeCompletedWrites().empty());
}

void narrowCapabilitiesPublishTypedClosedAndFaultedState() {
    svm::win32::Win32SerialSession session;
    svm::transport::SerialSession& lifecycle = session;
    svm::transport::SerialByteStream& byteStream = session;
    svm::transport::SerialWriteScheduler& scheduler = session;
    assert(&lifecycle.byteStream() == &byteStream);
    assert(&lifecycle.writeScheduler() == &scheduler);
    assert(lifecycle.snapshot().state == svm::transport::SerialSessionState::Closed);
    assert(lifecycle.snapshot().generation == 0);
    assert(lifecycle.snapshot().endpoint.empty());

    const auto closedWrite = byteStream.writeBytes({0x01}, deadlineAfter(100));
    assert(closedWrite.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(closedWrite.error.category == svm::transport::SerialErrorCategory::SessionClosed);
    assert(!closedWrite.operation.assigned());
    const auto closedAdmission = scheduler.enqueueWrite({0x02}, deadlineAfter(100));
    assert(closedAdmission.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(scheduler.writeQueueSnapshot().empty());

    svm::transport::SerialOpenOptions invalidOptions;
    const auto failedOpen = lifecycle.open(invalidOptions);
    assert(failedOpen.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    assert(failedOpen.error.category == svm::transport::SerialErrorCategory::InvalidInput);
    assert(lifecycle.snapshot().state == svm::transport::SerialSessionState::Faulted);
    assert(lifecycle.snapshot().generation == 0);
    assert(lifecycle.close().succeeded());
    assert(lifecycle.snapshot().state == svm::transport::SerialSessionState::Closed);
}

void nonClosedOpenIsRejectedWithoutMutatingTheSession() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 23, "COM23");
    const auto admission = Access::enqueueWithoutWorker(session, {0x01, 0x02}, deadlineAfter(500));
    assert(admission.accepted());
    const auto beforeSession = session.snapshot();
    const auto beforeQueue = session.writeQueueSnapshot();

    svm::transport::SerialOpenOptions replacementOptions;
    replacementOptions.portName = "COM24";
    const auto rejected = session.open(replacementOptions);

    assert(rejected.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    assert(rejected.error.category == svm::transport::SerialErrorCategory::InvalidInput);
    assert(!rejected.operation.assigned());
    assert(rejected.operation.generation == beforeSession.generation);
    assert(rejected.endpoint == beforeSession.endpoint);
    const auto afterSession = session.snapshot();
    const auto afterQueue = session.writeQueueSnapshot();
    assert(afterSession.state == beforeSession.state);
    assert(afterSession.generation == beforeSession.generation);
    assert(afterSession.endpoint == beforeSession.endpoint);
    assert(afterQueue.generation == beforeQueue.generation);
    assert(afterQueue.pendingCount == beforeQueue.pendingCount);
    assert(afterQueue.pendingBytes == beforeQueue.pendingBytes);
    assert(afterQueue.nextRequestId == beforeQueue.nextRequestId);
    assert(session.takeCompletedWrites().empty());

    assert(session.close().succeeded());
    const auto settled = session.takeCompletedWrites();
    assert(settled.size() == 1);
    assert(settled.front().operation.requestId == admission.requestId);
    assert(settled.front().operation.generation == beforeSession.generation);

    svm::win32::Win32SerialSession faultedSession;
    const auto failedOpen = faultedSession.open({});
    assert(failedOpen.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    const auto beforeRetry = faultedSession.snapshot();
    const auto rejectedRetry = faultedSession.open(replacementOptions);
    assert(rejectedRetry.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    assert(rejectedRetry.error.category == svm::transport::SerialErrorCategory::InvalidInput);
    assert(faultedSession.snapshot().state == beforeRetry.state);
    assert(faultedSession.snapshot().generation == beforeRetry.generation);
    assert(faultedSession.close().succeeded());
}

void nativeErrorClassificationUsesCodesInsteadOfLocalizedText() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    using Category = svm::transport::SerialErrorCategory;
    assert(Access::classifyNativeError(ERROR_SEM_TIMEOUT, false) == Category::Timeout);
    assert(Access::classifyNativeError(ERROR_TIMEOUT, false) == Category::Timeout);
    assert(Access::classifyNativeError(ERROR_OPERATION_ABORTED, true) == Category::Cancelled);
    assert(Access::classifyNativeError(ERROR_OPERATION_ABORTED, false) == Category::NativeFailure);
    assert(Access::classifyNativeError(ERROR_DEVICE_NOT_CONNECTED, false) == Category::Disconnected);
    assert(Access::classifyNativeError(ERROR_INVALID_HANDLE, false) == Category::Disconnected);
    assert(Access::classifyNativeError(ERROR_IO_DEVICE, false) == Category::Disconnected);
    assert(Access::classifyNativeError(123456, false) == Category::NativeFailure);
    assert(Access::classifyNativeError(0, false) == Category::IoFailure);
}

void expiredTypedWriteTimesOutBeforeNativeIo() {
    svm::win32::Win32SerialSession session;
    svm::win32::Win32SerialSessionTestAccess::publishSyntheticOpen(session, 4, "COM4");
    const svm::transport::SerialDeadline expired{
        .expiresAt = std::chrono::steady_clock::now() - std::chrono::milliseconds(1),
    };
    const auto result = session.writeBytes({0x01}, expired);
    assert(result.status == svm::transport::SerialOperationStatus::Timeout);
    assert(result.deadlineStatus == svm::transport::SerialDeadlineStatus::Expired);
    assert(result.error.category == svm::transport::SerialErrorCategory::Timeout);
    assert(result.operation.generation == 4);
    assert(result.endpoint == "COM4");
    assert(result.byteCount == 0);
    assert(session.close().succeeded());
}

void typedEmptyWriteIsRejectedBeforeNativeIo() {
    svm::win32::Win32SerialSession session;
    svm::win32::Win32SerialSessionTestAccess::publishSyntheticOpen(session, 5, "COM5");
    const auto result = session.writeBytes({});
    assert(result.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    assert(result.error.category == svm::transport::SerialErrorCategory::InvalidInput);
    assert(!result.operation.assigned());
    assert(result.operation.generation == 5);
    assert(session.close().succeeded());
}

void explicitCancellationUsesOneCompletionChannel() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 6, "COM6");
    const auto admission = Access::enqueueWithoutWorker(session, {0x01}, deadlineAfter(500));
    assert(admission.accepted());

    const auto cancelled = session.cancelPendingWrites();
    assert(cancelled.size() == 1);
    assert(cancelled.front().operation.requestId == admission.requestId);
    assert(cancelled.front().status == svm::transport::SerialOperationStatus::Cancelled);
    assert(session.takeCompletedWrites().empty());
    assert(session.close().succeeded());
}

void productionQueueSnapshotTracksLimitsIdentityAndHighWater() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 14, "COM14");

    auto snapshot = session.writeQueueSnapshot();
    assert(snapshot.generation == 14);
    assert(snapshot.capacity == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(snapshot.byteCapacity == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(snapshot.highWaterCount == 0);
    assert(snapshot.highWaterBytes == 0);

    std::vector<svm::transport::SerialWriteResult> countAdmissions;
    countAdmissions.reserve(svm::transport::kDefaultSerialWriteQueueCapacity);
    for (std::size_t index = 0; index < svm::transport::kDefaultSerialWriteQueueCapacity; ++index) {
        const auto admission = Access::enqueueWithoutWorker(
            session,
            {static_cast<std::uint8_t>(index)},
            deadlineAfter(500));
        assert(admission.accepted());
        assert(admission.generation == 14);
        assert(admission.requestId != svm::transport::kUnassignedSerialOperationId);
        if (!countAdmissions.empty()) {
            assert(admission.requestId == countAdmissions.back().requestId + 1);
        }
        countAdmissions.push_back(admission);
    }
    const auto beforeRejection = session.writeQueueSnapshot();
    const auto rejectionDeadline = deadlineAfter(500);
    const auto rejectionStarted = std::chrono::steady_clock::now();
    const auto rejected = session.enqueueWrite({0xFF}, rejectionDeadline);
    const auto rejectionElapsed = std::chrono::steady_clock::now() - rejectionStarted;
    snapshot = session.writeQueueSnapshot();
    assert(rejected.status == svm::transport::SerialOperationStatus::RejectedFull);
    assert(rejected.error.category == svm::transport::SerialErrorCategory::QueueFull);
    assert(rejected.operation.generation == 14);
    assert(rejected.operation.deadline.expiresAt == rejectionDeadline.expiresAt);
    assert(!rejected.operation.assigned());
    assert(snapshot.generation == 14);
    assert(snapshot.countedCount() == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(snapshot.highWaterCount == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(snapshot.highWaterBytes == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(snapshot.nextRequestId == beforeRejection.nextRequestId);
    assert(rejectionElapsed < std::chrono::milliseconds(
        svm::transport::kSerialTerminalResultTargetMs / 4));

    const auto active = Access::activateWithoutWorker(session);
    assert(active.has_value());
    assert(active->id == countAdmissions.front().requestId);
    assert(active->generation == countAdmissions.front().generation);
    snapshot = session.writeQueueSnapshot();
    assert(snapshot.activeRequestId == active->id);
    assert(snapshot.activeCount == 1);
    assert(snapshot.pendingCount == svm::transport::kDefaultSerialWriteQueueCapacity - 1);
    assert(snapshot.countedCount() == svm::transport::kDefaultSerialWriteQueueCapacity);

    const auto cancelled = session.cancelPendingWrites();
    assert(cancelled.size() == svm::transport::kDefaultSerialWriteQueueCapacity - 1);
    for (std::size_t index = 0; index < cancelled.size(); ++index) {
        assert(cancelled[index].operation.requestId == countAdmissions[index + 1].requestId);
        assert(cancelled[index].operation.generation == 14);
        assert(cancelled[index].operation.kind == svm::transport::SerialOperationKind::Write);
        assert(cancelled[index].operation.deadline.expiresAt
            == countAdmissions[index + 1].deadline.expiresAt);
        assert(cancelled[index].status == svm::transport::SerialOperationStatus::Cancelled);
        assert(cancelled[index].error.category == svm::transport::SerialErrorCategory::Cancelled);
        assert(cancelled[index].byteCount == 0);
        assert(cancelled[index].terminal());
    }
    assert(session.takeCompletedWrites().empty());
    assert(Access::completeActive(
        session,
        active->id,
        14,
        svm::transport::SerialWriteResultStatus::Sent,
        1));
    snapshot = session.writeQueueSnapshot();
    assert(snapshot.empty());
    assert(snapshot.generation == 14);
    assert(snapshot.activeRequestId == svm::transport::kUnassignedSerialOperationId);
    assert(snapshot.highWaterCount == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(snapshot.highWaterBytes == svm::transport::kDefaultSerialWriteQueueCapacity);
    const auto countResults = session.takeCompletedWrites();
    assert(countResults.size() == 1);
    assert(countResults.front().operation.requestId == countAdmissions.front().requestId);
    assert(countResults.front().operation.generation == 14);
    assert(countResults.front().status == svm::transport::SerialOperationStatus::Succeeded);
    assert(countResults.front().byteCount == 1);
    assert(countResults.front().terminal());
    assert(session.takeCompletedWrites().empty());
    assert(session.close().succeeded());

    Access::publishSyntheticOpen(session, 15, "COM15");
    snapshot = session.writeQueueSnapshot();
    assert(snapshot.generation == 15);
    assert(snapshot.highWaterCount == 0);
    assert(snapshot.highWaterBytes == 0);
    assert(session.close().succeeded());

    svm::win32::Win32SerialSession byteSession;
    Access::publishSyntheticOpen(byteSession, 16, "COM16");
    const std::size_t activePayloadBytes = svm::transport::kDefaultSerialWriteQueueByteCapacity / 2;
    const std::size_t firstPendingBytes = svm::transport::kDefaultSerialWriteQueueByteCapacity / 4;
    const std::size_t secondPendingBytes = svm::transport::kDefaultSerialWriteQueueByteCapacity
        - activePayloadBytes
        - firstPendingBytes;
    const auto byteActive = Access::enqueueWithoutWorker(
        byteSession,
        std::vector<std::uint8_t>(activePayloadBytes, 0x5A),
        deadlineAfter(500));
    assert(byteActive.accepted());
    const auto activatedBytes = Access::activateWithoutWorker(byteSession);
    assert(activatedBytes.has_value());
    assert(activatedBytes->id == byteActive.requestId);
    const auto firstBytePending = Access::enqueueWithoutWorker(
        byteSession,
        std::vector<std::uint8_t>(firstPendingBytes, 0xA5),
        deadlineAfter(500));
    const auto secondBytePending = Access::enqueueWithoutWorker(
        byteSession,
        std::vector<std::uint8_t>(secondPendingBytes, 0xC3),
        deadlineAfter(500));
    assert(firstBytePending.accepted());
    assert(secondBytePending.accepted());
    assert(firstBytePending.requestId == byteActive.requestId + 1);
    assert(secondBytePending.requestId == firstBytePending.requestId + 1);
    const auto beforeByteRejection = byteSession.writeQueueSnapshot();
    assert(beforeByteRejection.activeRequestId == byteActive.requestId);
    assert(beforeByteRejection.activeBytes == activePayloadBytes);
    assert(beforeByteRejection.pendingCount == 2);
    assert(beforeByteRejection.pendingBytes == firstPendingBytes + secondPendingBytes);
    assert(beforeByteRejection.countedBytes() == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    const auto byteRejectionDeadline = deadlineAfter(500);
    const auto byteRejectionStarted = std::chrono::steady_clock::now();
    const auto byteRejected = byteSession.enqueueWrite({0x01}, byteRejectionDeadline);
    const auto byteRejectionElapsed = std::chrono::steady_clock::now() - byteRejectionStarted;
    const auto byteSnapshot = byteSession.writeQueueSnapshot();
    assert(byteRejected.status == svm::transport::SerialOperationStatus::RejectedFull);
    assert(byteRejected.error.category == svm::transport::SerialErrorCategory::QueueFull);
    assert(byteRejected.operation.generation == 16);
    assert(byteRejected.operation.deadline.expiresAt == byteRejectionDeadline.expiresAt);
    assert(!byteRejected.operation.assigned());
    assert(byteRejectionElapsed < std::chrono::milliseconds(
        svm::transport::kSerialTerminalResultTargetMs / 4));
    assert(byteSnapshot.activeRequestId == byteActive.requestId);
    assert(byteSnapshot.pendingCount == 2);
    assert(byteSnapshot.countedBytes() == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(byteSnapshot.nextRequestId == beforeByteRejection.nextRequestId);
    assert(byteSnapshot.highWaterCount == 3);
    assert(byteSnapshot.highWaterBytes == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    const auto byteCancelled = byteSession.cancelPendingWrites();
    assert(byteCancelled.size() == 2);
    assert(byteCancelled[0].operation.requestId == firstBytePending.requestId);
    assert(byteCancelled[1].operation.requestId == secondBytePending.requestId);
    for (const auto& cancelledResult : byteCancelled) {
        assert(cancelledResult.operation.generation == 16);
        assert(cancelledResult.status == svm::transport::SerialOperationStatus::Cancelled);
        assert(cancelledResult.error.category == svm::transport::SerialErrorCategory::Cancelled);
        assert(cancelledResult.byteCount == 0);
        assert(cancelledResult.terminal());
    }
    assert(byteSession.takeCompletedWrites().empty());
    assert(Access::completeActive(
        byteSession,
        byteActive.requestId,
        16,
        svm::transport::SerialWriteResultStatus::Sent,
        activePayloadBytes));
    const auto byteResults = byteSession.takeCompletedWrites();
    assert(byteResults.size() == 1);
    assert(byteResults.front().operation.requestId == byteActive.requestId);
    assert(byteResults.front().operation.generation == 16);
    assert(byteResults.front().status == svm::transport::SerialOperationStatus::Succeeded);
    assert(byteResults.front().byteCount == activePayloadBytes);
    assert(byteResults.front().terminal());
    assert(byteSession.writeQueueSnapshot().empty());
    assert(byteSession.takeCompletedWrites().empty());
    assert(byteSession.close().succeeded());

    svm::win32::Win32SerialSession oversizedSession;
    Access::publishSyntheticOpen(oversizedSession, 17, "COM17");
    std::vector<std::uint8_t> oversizedPayload(
        svm::transport::kDefaultSerialWriteQueueByteCapacity + 1,
        0xA5);
    const auto oversized = oversizedSession.enqueueWrite(
        std::move(oversizedPayload),
        deadlineAfter(500));
    assert(oversized.status == svm::transport::SerialOperationStatus::RejectedFull);
    assert(oversized.error.category == svm::transport::SerialErrorCategory::QueueFull);
    assert(oversized.operation.generation == 17);
    assert(!oversized.operation.assigned());
    assert(oversizedSession.writeQueueSnapshot().empty());
    assert(oversizedSession.writeQueueSnapshot().nextRequestId == 1);
    assert(oversizedSession.writeQueueSnapshot().highWaterBytes == 0);
    assert(oversizedSession.close().succeeded());
}

void disconnectInvalidatesGenerationAndSettlesPendingWrites() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 7, "COM7");
    const auto activeAdmission = Access::enqueueWithoutWorker(session, {0x01}, deadlineAfter(500));
    const auto pendingAdmission = Access::enqueueWithoutWorker(session, {0x02}, deadlineAfter(600));
    assert(activeAdmission.accepted());
    assert(pendingAdmission.accepted());
    assert(Access::activateWithoutWorker(session).has_value());

    Access::markDisconnected(session, 7);
    const auto snapshot = session.snapshot();
    assert(snapshot.state == svm::transport::SerialSessionState::Faulted);
    assert(snapshot.generation == 0);
    assert(session.writeQueueSnapshot().activeCount == 1);
    assert(session.close().succeeded());

    const auto results = session.takeCompletedWrites();
    assert(results.size() == 2);
    assert(results[0].operation.requestId == pendingAdmission.requestId);
    assert(results[1].operation.requestId == activeAdmission.requestId);
    for (const auto& result : results) {
        assert(result.operation.generation == 7);
        assert(result.status == svm::transport::SerialOperationStatus::Disconnected);
        assert(result.error.category == svm::transport::SerialErrorCategory::Disconnected);
    }
    assert(session.takeCompletedWrites().empty());
}

void staleSnapshotCannotRewriteReplacementSessionEvidence() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    const auto oldSnapshot = session.snapshot();
    Access::publishSyntheticOpen(session, 9, "COM9");

    const auto rejected = Access::rejectFromSnapshot(session, oldSnapshot);
    assert(rejected.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(rejected.operation.generation == 0);
    assert(rejected.endpoint.empty());
    const auto current = session.snapshot();
    assert(current.state == svm::transport::SerialSessionState::Open);
    assert(current.generation == 9);
    assert(current.endpoint == "COM9");
    assert(session.close().succeeded());
}

void closeSettlesPendingAndActiveWritesExactlyOnce() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 8, "COM8");
    const auto activeAdmission = Access::enqueueWithoutWorker(session, {0x01, 0x02}, deadlineAfter(500));
    const auto pendingAdmission = Access::enqueueWithoutWorker(session, {0x03}, deadlineAfter(600));
    assert(activeAdmission.accepted());
    assert(pendingAdmission.accepted());
    assert(Access::activateWithoutWorker(session).has_value());
    assert(session.writeQueueSnapshot().activeCount == 1);
    assert(session.writeQueueSnapshot().pendingCount == 1);

    const auto closed = session.close();
    assert(closed.succeeded());
    assert(closed.operation.generation == 8);
    assert(session.snapshot().generation == 0);
    assert(session.writeQueueSnapshot().empty());

    const auto results = session.takeCompletedWrites();
    assert(results.size() == 2);
    assert(results[0].operation.requestId == pendingAdmission.requestId);
    assert(results[1].operation.requestId == activeAdmission.requestId);
    for (const auto& result : results) {
        assert(result.status == svm::transport::SerialOperationStatus::Cancelled);
        assert(result.error.category == svm::transport::SerialErrorCategory::SessionClosed);
        assert(result.operation.generation == 8);
        assert(result.endpoint == "COM8");
    }
    assert(session.takeCompletedWrites().empty());
}

void staleOrDuplicateCompletionCannotReleaseTheActiveReservation() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 12, "COM12");
    const auto admission = Access::enqueueWithoutWorker(session, {0x01, 0x02}, deadlineAfter(500));
    assert(Access::activateWithoutWorker(session).has_value());
    const auto before = session.writeQueueSnapshot();

    assert(!Access::completeActive(
        session,
        admission.requestId,
        11,
        svm::transport::SerialWriteResultStatus::Sent,
        2));
    assert(session.writeQueueSnapshot().activeCount == before.activeCount);
    assert(session.writeQueueSnapshot().activeBytes == before.activeBytes);
    assert(session.takeCompletedWrites().empty());

    assert(Access::completeActive(
        session,
        admission.requestId,
        12,
        svm::transport::SerialWriteResultStatus::Sent,
        2));
    assert(!Access::completeActive(
        session,
        admission.requestId,
        12,
        svm::transport::SerialWriteResultStatus::Sent,
        2));
    const auto results = session.takeCompletedWrites();
    assert(results.size() == 1);
    assert(results.front().status == svm::transport::SerialOperationStatus::Succeeded);
    assert(session.writeQueueSnapshot().empty());
    assert(session.close().succeeded());
}

void everySessionTerminalPathPublishesExactlyOnce() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    using Category = svm::transport::SerialErrorCategory;
    using OperationStatus = svm::transport::SerialOperationStatus;
    using QueueStatus = svm::transport::SerialWriteResultStatus;
    struct TerminalCase {
        QueueStatus queueStatus;
        Category category;
        OperationStatus operationStatus;
        std::uint32_t nativeCode;
        std::size_t byteCount;
    };
    constexpr std::array cases{
        TerminalCase{QueueStatus::Sent, Category::None, OperationStatus::Succeeded, 0, 2},
        TerminalCase{QueueStatus::Failed, Category::NativeFailure, OperationStatus::Failed, ERROR_WRITE_FAULT, 1},
        TerminalCase{QueueStatus::Timeout, Category::Timeout, OperationStatus::Timeout, ERROR_SEM_TIMEOUT, 1},
        TerminalCase{QueueStatus::Cancelled, Category::Cancelled, OperationStatus::Cancelled, ERROR_OPERATION_ABORTED, 1},
        TerminalCase{QueueStatus::Disconnected, Category::Disconnected, OperationStatus::Disconnected, ERROR_DEVICE_NOT_CONNECTED, 1},
        TerminalCase{QueueStatus::Closed, Category::SessionClosed, OperationStatus::Cancelled, 0, 1},
    };

    std::size_t index = 0;
    for (const TerminalCase& terminalCase : cases) {
        svm::win32::Win32SerialSession session;
        const auto generation = static_cast<svm::transport::SerialSessionGeneration>(20 + index);
        const std::string endpoint = "COM" + std::to_string(generation);
        Access::publishSyntheticOpen(session, generation, endpoint);
        const auto admission = Access::enqueueWithoutWorker(
            session,
            {0x01, 0x02},
            deadlineAfter(5000));
        assert(admission.accepted());
        assert(Access::activateWithoutWorker(session).has_value());

        assert(Access::completeActive(
            session,
            admission.requestId,
            generation,
            terminalCase.queueStatus,
            terminalCase.byteCount,
            terminalCase.category,
            terminalCase.nativeCode));
        assert(!Access::completeActive(
            session,
            admission.requestId,
            generation,
            terminalCase.queueStatus,
            terminalCase.byteCount,
            terminalCase.category,
            terminalCase.nativeCode));

        const auto results = session.takeCompletedWrites();
        assert(results.size() == 1);
        const auto& result = results.front();
        assert(result.operation.requestId == admission.requestId);
        assert(result.operation.generation == generation);
        assert(result.operation.kind == svm::transport::SerialOperationKind::Write);
        assert(result.operation.deadline.expiresAt == admission.deadline.expiresAt);
        assert(result.status == terminalCase.operationStatus);
        assert(result.byteCount == terminalCase.byteCount);
        assert(result.endpoint == endpoint);
        assert(result.error.category == terminalCase.category);
        assert(result.error.nativeCode == terminalCase.nativeCode);
        assert(result.error.byteCount == terminalCase.byteCount);
        assert(session.takeCompletedWrites().empty());
        assert(session.writeQueueSnapshot().empty());
        assert(session.close().succeeded());
        ++index;
    }
}

void directAndQueuedWritesUseDistinctRequestIds() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 30, "COM30");

    const auto requestedDeadline = deadlineAfter(5000);
    const auto direct = session.writeBytes({0x01}, requestedDeadline);
    assert(direct.terminal());
    assert(direct.operation.assigned());
    assert(direct.operation.generation == 30);
    assert(direct.operation.deadline.set());
    assert(direct.operation.deadline.expiresAt < requestedDeadline.expiresAt);

    const auto queued = session.enqueueWrite({0x02}, requestedDeadline);
    assert(queued.accepted());
    assert(queued.operation.requestId != direct.operation.requestId);
    assert(queued.operation.deadline.set());
    assert(queued.operation.deadline.expiresAt < requestedDeadline.expiresAt);
    assert(session.close().succeeded());
    const auto completed = session.takeCompletedWrites();
    assert(completed.size() == 1);
    assert(completed.front().operation.requestId == queued.operation.requestId);
    assert(session.takeCompletedWrites().empty());
}

void reconnectNeverReplaysSettledRequests() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 40, "COM40");
    const auto oldActive = Access::enqueueWithoutWorker(session, {0x01}, deadlineAfter(5000));
    const auto oldPending = Access::enqueueWithoutWorker(session, {0x02}, deadlineAfter(5000));
    assert(oldActive.accepted());
    assert(oldPending.accepted());
    assert(Access::activateWithoutWorker(session).has_value());

    const auto closeStarted = std::chrono::steady_clock::now();
    assert(session.close().succeeded());
    assert(std::chrono::steady_clock::now() - closeStarted
        < std::chrono::milliseconds(svm::transport::kSerialTerminalResultTargetMs));
    const auto oldResults = session.takeCompletedWrites();
    assert(oldResults.size() == 2);
    assert(oldResults[0].operation.requestId == oldPending.requestId);
    assert(oldResults[1].operation.requestId == oldActive.requestId);
    assert(session.takeCompletedWrites().empty());

    Access::publishSyntheticOpen(session, 41, "COM41");
    assert(session.writeQueueSnapshot().empty());
    assert(session.takeCompletedWrites().empty());
    const auto replacement = Access::enqueueWithoutWorker(session, {0x03}, deadlineAfter(5000));
    assert(replacement.accepted());
    assert(replacement.requestId != oldActive.requestId);
    assert(replacement.requestId != oldPending.requestId);
    const auto replacementActive = Access::activateWithoutWorker(session);
    assert(replacementActive.has_value());
    assert(replacementActive->id == replacement.requestId);
    assert(replacementActive->generation == 41);
    const auto beforeStaleCompletion = session.writeQueueSnapshot();

    assert(!Access::completeActive(
        session,
        oldActive.requestId,
        40,
        svm::transport::SerialWriteResultStatus::Sent,
        1));
    const auto afterStaleCompletion = session.writeQueueSnapshot();
    assert(afterStaleCompletion.generation == beforeStaleCompletion.generation);
    assert(afterStaleCompletion.activeRequestId == replacement.requestId);
    assert(afterStaleCompletion.activeCount == beforeStaleCompletion.activeCount);
    assert(afterStaleCompletion.activeBytes == beforeStaleCompletion.activeBytes);
    assert(afterStaleCompletion.pendingCount == beforeStaleCompletion.pendingCount);
    assert(afterStaleCompletion.pendingBytes == beforeStaleCompletion.pendingBytes);
    assert(afterStaleCompletion.nextRequestId == beforeStaleCompletion.nextRequestId);
    assert(session.takeCompletedWrites().empty());
    assert(session.close().succeeded());

    const auto replacementResults = session.takeCompletedWrites();
    assert(replacementResults.size() == 1);
    assert(replacementResults.front().operation.requestId == replacement.requestId);
    assert(replacementResults.front().operation.generation == 41);
    assert(session.takeCompletedWrites().empty());
}

void closeWaitPublishesOnceAndRetainsNativeOwnership() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    HANDLE releaseEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    assert(releaseEvent != nullptr);

    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 50, "COM50");
    const auto admission = Access::enqueueWithoutWorker(session, {0x01}, deadlineAfter(5000));
    assert(admission.accepted());
    assert(Access::activateWithoutWorker(session).has_value());
    assert(Access::installBlockedWorker(session, releaseEvent));

    const auto closeStarted = std::chrono::steady_clock::now();
    const auto firstClose = session.close();
    const auto closeElapsed = std::chrono::steady_clock::now() - closeStarted;
    assert(firstClose.status == svm::transport::SerialOperationStatus::Failed);
    assert(firstClose.error.category == svm::transport::SerialErrorCategory::NativeFailure);
    assert(firstClose.error.nativeCode == ERROR_TIMEOUT);
    assert(closeElapsed >= std::chrono::milliseconds(
        svm::transport::kSerialTerminalResultTargetMs / 4));
    assert(closeElapsed < std::chrono::milliseconds(
        svm::transport::kSerialTerminalResultTargetMs));
    assert(Access::hasWorkerResources(session));
    assert(session.writeQueueSnapshot().activeCount == 1);
    const auto logicalResults = session.takeCompletedWrites();
    assert(logicalResults.size() == 1);
    assert(logicalResults.front().operation.requestId == admission.requestId);
    assert(logicalResults.front().operation.generation == 50);
    assert(logicalResults.front().status == svm::transport::SerialOperationStatus::Cancelled);
    assert(logicalResults.front().error.category == svm::transport::SerialErrorCategory::SessionClosed);
    assert(logicalResults.front().error.nativeCode == ERROR_TIMEOUT);
    assert(session.takeCompletedWrites().empty());

    assert(SetEvent(releaseEvent));
    assert(session.close().succeeded());
    assert(session.takeCompletedWrites().empty());
    assert(session.writeQueueSnapshot().empty());
    assert(!Access::hasWorkerResources(session));
    assert(CloseHandle(releaseEvent));
}

void exitedWorkerIsReapedBeforeNewAdmission() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 13, "COM13");
    assert(Access::installExitedWorker(session));

    const auto admission = session.enqueueWrite({0x01}, deadlineAfter(500));
    assert(admission.accepted());
    assert(admission.error.category == svm::transport::SerialErrorCategory::None);
    assert(session.close().succeeded());
    assert(session.writeQueueSnapshot().empty());

    const auto completed = session.takeCompletedWrites();
    assert(completed.size() == 1);
    assert(completed.front().operation.requestId == admission.operation.requestId);
    assert(completed.front().terminal());
}
#endif

} // namespace

int main() {
    validatesSerialOpenOptions();
    normalizesComPortNamesAndDevicePaths();
    keepsChineseNamesForSettings();
    translatesCommonWin32ErrorsToActionableChinese();
#if defined(_WIN32)
    closedPortAsyncWriteFailsWithoutQueueing();
    narrowCapabilitiesPublishTypedClosedAndFaultedState();
    nonClosedOpenIsRejectedWithoutMutatingTheSession();
    nativeErrorClassificationUsesCodesInsteadOfLocalizedText();
    expiredTypedWriteTimesOutBeforeNativeIo();
    typedEmptyWriteIsRejectedBeforeNativeIo();
    explicitCancellationUsesOneCompletionChannel();
    productionQueueSnapshotTracksLimitsIdentityAndHighWater();
    disconnectInvalidatesGenerationAndSettlesPendingWrites();
    staleSnapshotCannotRewriteReplacementSessionEvidence();
    closeSettlesPendingAndActiveWritesExactlyOnce();
    staleOrDuplicateCompletionCannotReleaseTheActiveReservation();
    everySessionTerminalPathPublishesExactlyOnce();
    directAndQueuedWritesUseDistinctRequestIds();
    reconnectNeverReplaysSettledRequests();
    closeWaitPublishesOnceAndRetainsNativeOwnership();
    exitedWorkerIsReapedBeforeNewAdmission();
#endif

    std::cout << "native_win32_serial_tests passed\n";
    return 0;
}
