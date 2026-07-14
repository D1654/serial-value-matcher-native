#include "win32/win32_serial_types.h"
#if defined(_WIN32)
#include "transport/serial_transport.h"
#include "win32/win32_serial_session.h"
#endif

#include <algorithm>
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
    static void publishSyntheticOpen(
        Win32SerialSession& session,
        svm::transport::SerialSessionGeneration generation,
        std::string endpoint) {
        EnterCriticalSection(&session.writeLock_);
        session.state_ = svm::transport::SerialSessionState::Open;
        session.generation_ = generation;
        session.generationCounter_ = std::max(session.generationCounter_, generation);
        session.options_.portName = std::move(endpoint);
        session.lastErrorText_.clear();
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
        std::size_t byteCount) {
        EnterCriticalSection(&session.writeLock_);
        if (!session.activeWrite_.has_value()) {
            LeaveCriticalSection(&session.writeLock_);
            return false;
        }
        Win32SerialSession::ActiveWrite completion = session.activeWrite_.value();
        completion.requestId = requestId;
        completion.generation = generation;
        const bool completed = session.completeActiveWrite(
            completion,
            status,
            byteCount,
            status == svm::transport::SerialWriteResultStatus::Timeout
                ? svm::transport::SerialErrorCategory::Timeout
                : svm::transport::SerialErrorCategory::None,
            0,
            {});
        if (completed) {
            session.activeWrite_.reset();
        }
        LeaveCriticalSection(&session.writeLock_);
        return completed;
    }

    static svm::transport::SerialErrorCategory classifyNativeError(
        std::uint32_t nativeCode,
        bool cancellationRequested) noexcept {
        return Win32SerialSession::nativeErrorCategory(nativeCode, cancellationRequested);
    }

    static void markDisconnected(
        Win32SerialSession& session,
        svm::transport::SerialSessionGeneration generation) {
        session.markGenerationDisconnected(generation, "设备已断开");
    }

    static svm::transport::SerialOperationResult rejectFromSnapshot(
        Win32SerialSession& session,
        const svm::transport::SerialSessionSnapshot& snapshot) {
        return session.rejectedClosed(svm::transport::SerialOperationKind::Read, snapshot);
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
    options.writeTimeoutMs = -1;
    const auto badWriteTimeout = svm::win32::validateSerialOpenOptions(options);
    assert(!badWriteTimeout.ok);
    assert(contains(badWriteTimeout.errorMessage, "超时"));

    options.writeTimeoutMs = 1000;
    options.readBufferSize = 0;
    const auto badBuffer = svm::win32::validateSerialOpenOptions(options);
    assert(!badBuffer.ok);
    assert(contains(badBuffer.errorMessage, "缓冲区"));
}

void normalizesComPortNamesAndDevicePaths() {
    assert(svm::win32::trimPortName("  COM10 \t") == "COM10");
    assert(svm::win32::normalizedComPortName(" com3 ") == "COM3");
    assert(svm::win32::normalizedComPortName(R"(\\.\com42)") == "COM42");
    assert(svm::win32::comPortNumber("COM1") == 1);
    assert(svm::win32::comPortNumber(R"(\\.\COM256)") == 256);
    assert(svm::win32::comPortNumber("COM0") == -1);
    assert(svm::win32::comPortNumber("COM") == -1);

    assert(svm::win32::isLikelyComPortName("COM12"));
    assert(!svm::win32::isLikelyComPortName("ttyS0"));
    assert(svm::win32::isWin32DevicePath(R"(\\.\COM12)"));
    assert(svm::win32::makeWin32DevicePath("COM10") == R"(\\.\COM10)");
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
    static_assert(std::derived_from<svm::win32::Win32SerialSession, svm::transport::SerialTransport>);
    static_assert(!std::copy_constructible<svm::win32::Win32SerialSession>);
    static_assert(!std::movable<svm::win32::Win32SerialSession>);
    svm::transport::SerialTransport& transport = session;
    const auto result = transport.enqueueWrite(std::vector<std::uint8_t>{0x01});
    assert(result.status == svm::transport::SerialWriteResultStatus::Failed);
    assert(!result.message.empty());

    const auto snapshot = transport.writeQueueSnapshot();
    assert(snapshot.pendingCount == 0);
    assert(transport.takeCompletedWrites().empty());
}

void capabilityViewPublishesTypedClosedAndFaultedState() {
    svm::win32::Win32SerialSession session;
    svm::transport::SerialSession& capability = session.sessionCapability();
    assert(&capability.byteStream() != nullptr);
    assert(&capability.writeScheduler() != nullptr);
    assert(capability.snapshot().state == svm::transport::SerialSessionState::Closed);
    assert(capability.snapshot().generation == 0);
    assert(capability.snapshot().endpoint.empty());

    const auto closedWrite = capability.byteStream().writeBytes({0x01}, deadlineAfter(100));
    assert(closedWrite.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(closedWrite.error.category == svm::transport::SerialErrorCategory::SessionClosed);
    assert(!closedWrite.operation.assigned());
    const auto closedAdmission = capability.writeScheduler().enqueueWrite({0x02}, deadlineAfter(100));
    assert(closedAdmission.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(capability.writeScheduler().writeQueueSnapshot().empty());

    svm::transport::SerialOpenOptions invalidOptions;
    const auto failedOpen = capability.open(invalidOptions);
    assert(failedOpen.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    assert(failedOpen.error.category == svm::transport::SerialErrorCategory::InvalidInput);
    assert(capability.snapshot().state == svm::transport::SerialSessionState::Faulted);
    assert(capability.snapshot().generation == 0);
    assert(capability.close().succeeded());
    assert(capability.snapshot().state == svm::transport::SerialSessionState::Closed);
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
    assert(Access::classifyNativeError(123456, false) == Category::NativeFailure);
    assert(Access::classifyNativeError(0, false) == Category::IoFailure);
}

void expiredTypedWriteTimesOutBeforeNativeIo() {
    svm::win32::Win32SerialSession session;
    svm::win32::Win32SerialSessionTestAccess::publishSyntheticOpen(session, 4, "COM4");
    const svm::transport::SerialDeadline expired{
        .expiresAt = std::chrono::steady_clock::now() - std::chrono::milliseconds(1),
    };
    const auto result = session.sessionCapability().byteStream().writeBytes({0x01}, expired);
    assert(result.status == svm::transport::SerialOperationStatus::Timeout);
    assert(result.deadlineStatus == svm::transport::SerialDeadlineStatus::Expired);
    assert(result.error.category == svm::transport::SerialErrorCategory::Timeout);
    assert(result.operation.generation == 4);
    assert(result.endpoint == "COM4");
    assert(result.byteCount == 0);
    assert(session.sessionCapability().close().succeeded());
}

void typedEmptyWriteIsRejectedBeforeNativeIo() {
    svm::win32::Win32SerialSession session;
    svm::win32::Win32SerialSessionTestAccess::publishSyntheticOpen(session, 5, "COM5");
    const auto result = session.sessionCapability().byteStream().writeBytes({});
    assert(result.status == svm::transport::SerialOperationStatus::RejectedInvalid);
    assert(result.error.category == svm::transport::SerialErrorCategory::InvalidInput);
    assert(!result.operation.assigned());
    assert(result.operation.generation == 5);
    assert(session.sessionCapability().close().succeeded());
}

void explicitCancellationUsesOneCompletionChannel() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    Access::publishSyntheticOpen(session, 6, "COM6");
    const auto admission = Access::enqueueWithoutWorker(session, {0x01}, deadlineAfter(500));
    assert(admission.accepted());

    const auto cancelled = session.sessionCapability().writeScheduler().cancelPendingWrites();
    assert(cancelled.size() == 1);
    assert(cancelled.front().operation.requestId == admission.requestId);
    assert(cancelled.front().status == svm::transport::SerialOperationStatus::Cancelled);
    assert(session.sessionCapability().writeScheduler().takeCompletedWrites().empty());
    assert(session.sessionCapability().close().succeeded());
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
    const auto snapshot = session.sessionCapability().snapshot();
    assert(snapshot.state == svm::transport::SerialSessionState::Faulted);
    assert(snapshot.generation == 0);
    assert(session.writeQueueSnapshot().activeCount == 1);
    assert(session.sessionCapability().close().succeeded());

    const auto results = session.sessionCapability().writeScheduler().takeCompletedWrites();
    assert(results.size() == 2);
    assert(results[0].operation.requestId == pendingAdmission.requestId);
    assert(results[1].operation.requestId == activeAdmission.requestId);
    for (const auto& result : results) {
        assert(result.operation.generation == 7);
        assert(result.status == svm::transport::SerialOperationStatus::Disconnected);
        assert(result.error.category == svm::transport::SerialErrorCategory::Disconnected);
    }
    assert(session.sessionCapability().writeScheduler().takeCompletedWrites().empty());
}

void staleSnapshotCannotRewriteReplacementSessionEvidence() {
    using Access = svm::win32::Win32SerialSessionTestAccess;
    svm::win32::Win32SerialSession session;
    const auto oldSnapshot = session.sessionCapability().snapshot();
    Access::publishSyntheticOpen(session, 9, "COM9");

    const auto rejected = Access::rejectFromSnapshot(session, oldSnapshot);
    assert(rejected.status == svm::transport::SerialOperationStatus::RejectedClosed);
    assert(rejected.operation.generation == 0);
    assert(rejected.endpoint.empty());
    assert(session.sessionCapability().snapshot().generation == 9);
    assert(session.lastErrorText().empty());
    assert(session.sessionCapability().close().succeeded());
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

    const auto closed = session.sessionCapability().close();
    assert(closed.succeeded());
    assert(closed.operation.generation == 8);
    assert(session.sessionCapability().snapshot().generation == 0);
    assert(session.writeQueueSnapshot().empty());

    const auto results = session.sessionCapability().writeScheduler().takeCompletedWrites();
    assert(results.size() == 2);
    assert(results[0].operation.requestId == pendingAdmission.requestId);
    assert(results[1].operation.requestId == activeAdmission.requestId);
    for (const auto& result : results) {
        assert(result.status == svm::transport::SerialOperationStatus::Cancelled);
        assert(result.error.category == svm::transport::SerialErrorCategory::SessionClosed);
        assert(result.operation.generation == 8);
        assert(result.endpoint == "COM8");
    }
    assert(session.sessionCapability().writeScheduler().takeCompletedWrites().empty());
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
    assert(session.sessionCapability().writeScheduler().takeCompletedWrites().empty());

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
    const auto results = session.sessionCapability().writeScheduler().takeCompletedWrites();
    assert(results.size() == 1);
    assert(results.front().status == svm::transport::SerialOperationStatus::Succeeded);
    assert(session.writeQueueSnapshot().empty());
    assert(session.sessionCapability().close().succeeded());
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
    capabilityViewPublishesTypedClosedAndFaultedState();
    nativeErrorClassificationUsesCodesInsteadOfLocalizedText();
    expiredTypedWriteTimesOutBeforeNativeIo();
    typedEmptyWriteIsRejectedBeforeNativeIo();
    explicitCancellationUsesOneCompletionChannel();
    disconnectInvalidatesGenerationAndSettlesPendingWrites();
    staleSnapshotCannotRewriteReplacementSessionEvidence();
    closeSettlesPendingAndActiveWritesExactlyOnce();
    staleOrDuplicateCompletionCannotReleaseTheActiveReservation();
#endif

    std::cout << "native_win32_serial_tests passed\n";
    return 0;
}
