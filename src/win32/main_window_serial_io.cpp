#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_time_utils.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_enumerator.h"
#include "win32/win32_serial_types.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

std::wstring writeResultMessage(const svm::transport::SerialOperationResult& result) {
    switch (result.status) {
    case svm::transport::SerialOperationStatus::Accepted:
        return L"串口写入请求已接受。";
    case svm::transport::SerialOperationStatus::Succeeded:
        return {};
    case svm::transport::SerialOperationStatus::RejectedInvalid:
        return L"串口写入请求无效。";
    case svm::transport::SerialOperationStatus::RejectedFull:
        return L"串口写入队列已满。";
    case svm::transport::SerialOperationStatus::RejectedClosed:
        return L"串口会话已关闭，无法写入。";
    case svm::transport::SerialOperationStatus::Timeout:
        return L"串口写入超时。";
    case svm::transport::SerialOperationStatus::Cancelled:
        return L"串口写入已取消。";
    case svm::transport::SerialOperationStatus::Disconnected:
        return result.error.nativeCode == 0
            ? std::wstring(L"串口设备已断开。")
            : utf8ToWide(win32SerialErrorText(result.error.nativeCode, "写入串口"));
    case svm::transport::SerialOperationStatus::Failed:
        break;
    }
    if (result.error.nativeCode != 0
        && result.error.category == svm::transport::SerialErrorCategory::NativeFailure) {
        return utf8ToWide(win32SerialErrorText(result.error.nativeCode, "写入串口"));
    }
    if (result.byteCount > 0) {
        return L"串口写入在发送 " + std::to_wstring(result.byteCount) + L" 字节后失败。";
    }
    return L"串口写入失败。";
}

std::wstring readResultMessage(const svm::transport::SerialOperationResult& result) {
    if (result.status == svm::transport::SerialOperationStatus::Disconnected) {
        return result.error.nativeCode == 0
            ? std::wstring(L"串口设备已断开。")
            : utf8ToWide(win32SerialErrorText(result.error.nativeCode, "读取串口"));
    }
    if (result.error.nativeCode != 0
        && result.error.category == svm::transport::SerialErrorCategory::NativeFailure) {
        return utf8ToWide(win32SerialErrorText(result.error.nativeCode, "读取串口"));
    }
    if (result.error.category == svm::transport::SerialErrorCategory::IoFailure) {
        return L"串口接收状态异常。";
    }
    return L"串口读取失败。";
}

} // namespace

void NativeMainWindow::updateSerialWriteQueueStatus() {
    serialIoState_.updateWriteQueueSnapshot(serialWriteScheduler_.writeQueueSnapshot());
}

void NativeMainWindow::clearPendingSerialWrites() {
    pendingSerialWrites_.clear();
    serialWriteScheduler_.takeCompletedWrites();
    updateSerialWriteQueueStatus();
}

void NativeMainWindow::drainSerialWriteResults() {
    const std::vector<svm::transport::SerialTerminalResult> results =
        serialWriteScheduler_.takeCompletedWrites();
    if (results.empty()) {
        updateSerialWriteQueueStatus();
        return;
    }

    for (const auto& result : results) {
        if (!result.terminal()
            || !result.operation.assigned()
            || result.operation.kind != svm::transport::SerialOperationKind::Write) {
            continue;
        }
        const auto found = std::find_if(
            pendingSerialWrites_.begin(),
            pendingSerialWrites_.end(),
            [&result](const NativePendingSerialWrite& pending) {
                return pending.key.matches(result.operation);
            });
        if (found == pendingSerialWrites_.end()) {
            continue;
        }

        NativePendingSerialWrite pending = std::move(*found);
        pendingSerialWrites_.erase(found);

        const svm::transport::SerialSessionSnapshot currentSession = serialLifecycle_.snapshot();
        const auto& queueSnapshot = serialIoState_.writeQueueSnapshot();
        const NativeSerialWriteCompletionDecision decision =
            nativeSerialWriteCompletionDecision(
                pending.key,
                result,
                currentSession,
                queueSnapshot.has_value()
                    ? queueSnapshot->generation
                    : svm::transport::kUnassignedSerialSessionGeneration);
        if (decision == NativeSerialWriteCompletionDecision::Ignore) {
            continue;
        }

        if (decision == NativeSerialWriteCompletionDecision::Cancelled) {
            setStatus(writeResultMessage(result));
            continue;
        }

        if (decision == NativeSerialWriteCompletionDecision::Failed) {
            const std::wstring message = writeResultMessage(result);
            if (pending.source == NativePendingSerialWriteSource::File && fileSend_.active()) {
                stopFileSend(message);
            } else {
                setStatus(message);
            }
            handleSerialFailure(wideToUtf8(message));
            continue;
        }

        if (pending.source == NativePendingSerialWriteSource::Manual) {
            saveRawEvent("Tx", result.endpoint, pending.payload);
            if (pending.saveHistory && store_.isOpen()) {
                native_storage::SendHistoryEntry history = nativeMakeSendHistoryEntry(
                    wideToUtf8(pending.manualText),
                    pending.payloadMode,
                    pending.lineEnding,
                    pending.textCodePage,
                    nativeUtcTimestampText());
                store_.saveSendHistory(history);
                refreshSendHistory();
            }
            appendPayloadLog(NativeLogKind::Tx, pending.payload);
            statusCountersState_.addTxBytes(static_cast<std::uint64_t>(result.byteCount));
            updateStatusSegments();
            setStatus(uiString(T::SentPrefix) + std::to_wstring(result.byteCount) + uiString(T::BytesSuffix));
            continue;
        }

        if (!fileSend_.active()
            || fileSendGeneration_ != result.operation.generation) {
            continue;
        }

        saveRawEvent("Tx", result.endpoint, pending.payload);
        appendPayloadLog(NativeLogKind::Tx, pending.payload);
        statusCountersState_.addTxBytes(static_cast<std::uint64_t>(result.byteCount));
        updateStatusSegments();
        fileSend_.markBytesWritten(result.byteCount);
        updateFileSendProgress();

        if (fileSend_.done()) {
            const std::wstring summary = uiString(T::FileSendDonePrefix) + std::to_wstring(fileSend_.sentBytes()) + uiString(T::BytesSuffix);
            stopFileSend(summary);
            continue;
        }

        if ((fileSend_.sentBytes() % static_cast<std::uintmax_t>(kNativeFileSendChunkBytes * 16)) == 0) {
            setStatus(uiString(T::FileSendProgressPrefix)
                + std::to_wstring(fileSend_.sentBytes())
                + L"/"
                + std::to_wstring(fileSend_.totalBytes())
                + uiString(T::BytesSuffix));
        }
    }

    updateSerialWriteQueueStatus();
}

void NativeMainWindow::pollSerial() {
    drainSerialWriteResults();
    const svm::transport::SerialSessionSnapshot polledSession = serialLifecycle_.snapshot();
    if (!polledSession.open()) {
        return;
    }
    if (!serialIoState_.allowsSerialPoll()) {
        return;
    }
    std::vector<native_storage::RawIoEvent> events;
    std::vector<std::uint8_t> mergedPayload;
    std::optional<std::wstring> failureMessage;
    std::optional<std::wstring> statusMessage;
    for (int batch = 0; batch < 8; ++batch) {
        svm::transport::SerialReadResult read = serialByteStream_.readAvailable(4096);
        const NativeSerialReadDecision decision =
            nativeSerialReadDecision(read, polledSession.generation);
        if (decision == NativeSerialReadDecision::Stop) {
            break;
        }
        if (decision == NativeSerialReadDecision::Fail) {
            failureMessage = readResultMessage(read.operation);
            break;
        }
        if (decision == NativeSerialReadDecision::ReportError) {
            statusMessage = readResultMessage(read.operation);
            break;
        }
        native_storage::RawIoEvent event;
        event.sessionId = sessionId_;
        event.direction = "Rx";
        event.timestampUtc = nativeUtcTimestampText();
        event.endpoint = read.operation.endpoint;
        event.payload = read.bytes;
        events.push_back(std::move(event));
        mergedPayload.insert(mergedPayload.end(), read.bytes.begin(), read.bytes.end());
    }

    const svm::transport::SerialSessionSnapshot currentSession = serialLifecycle_.snapshot();
    if (currentSession.open() && currentSession.generation != polledSession.generation) {
        return;
    }
    if (!mergedPayload.empty()) {
        saveRawEvents(std::move(events));
        appendPayloadLog(NativeLogKind::Rx, mergedPayload);
        statusCountersState_.addRxBytes(static_cast<std::uint64_t>(mergedPayload.size()));
        updateStatusSegments();
    }
    if (failureMessage.has_value()) {
        handleSerialFailure(wideToUtf8(*failureMessage));
    } else if (statusMessage.has_value()) {
        setStatus(*statusMessage);
    }
}

void NativeMainWindow::handleSerialFailure(const std::string& message) {
    const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
    if (snapshot.state == svm::transport::SerialSessionState::Closed) {
        return;
    }
    const std::string endpoint = snapshot.endpoint;
    KillTimer(window_, IDT_TIMED_SEND);
    timedSendConfirmed_ = false;
    stopFileSend({});
    serialLifecycle_.close();
    clearPendingSerialWrites();
    updateConnectionButtonState();
    updateRtsControlState();
    appendLog(NativeLogKind::Error, uiString(T::SystemSerialFailedPrefix) + utf8ToWide(message));
    const bool autoReconnect = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (autoReconnect && reconnectState_.hasLastOpenOptions()) {
        reconnectState_.startWaiting(endpoint);
        SetTimer(window_, IDT_RECONNECT, 2000, nullptr);
        setStatus(uiString(T::ReconnectWaitingPrefix) + utf8ToWide(endpoint));
    } else {
        setStatus(utf8ToWide(message));
    }
}

void NativeMainWindow::tryAutoReconnect() {
    const bool autoReconnect = SendMessageW(autoReconnectCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!autoReconnect) {
        reconnectState_.clearWaiting();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }
    if (!reconnectState_.shouldTryReconnect(serialLifecycle_.snapshot().open())) {
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    const auto ports = Win32SerialEnumerator::availablePorts();
    const bool present = std::any_of(ports.begin(), ports.end(), [this](const SerialPortDescriptor& port) {
        return normalizedComPortName(port.portName) == normalizedComPortName(reconnectState_.reconnectPortName());
    });
    if (!present) {
        setStatus(uiString(T::WaitingReconnectPrefix) + utf8ToWide(reconnectState_.reconnectPortName()) + uiString(T::PortNotReadySuffix));
        return;
    }

    const std::optional<SerialOpenOptions> options = reconnectState_.reconnectOptions();
    if (!options.has_value()) {
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }
    const svm::transport::SerialOperationResult openResult = serialLifecycle_.open(*options);
    if (!openResult.succeeded()) {
        setStatus(uiString(T::AutoReconnectFailedPrefix)
            + serialOperationErrorMessage(openResult, "自动重连串口"));
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    const svm::transport::SerialSessionSnapshot openedSession = serialLifecycle_.snapshot();
    if (!openedSession.open() || openedSession.generation != openResult.operation.generation) {
        serialLifecycle_.close();
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        setStatus(uiString(T::AutoReconnectFailedPrefix) + L"会话状态未能稳定发布。");
        return;
    }

    reconnectState_.rememberSuccessfulOpen(openedSession.options);
    KillTimer(window_, IDT_RECONNECT);
    updateConnectionButtonState();
    updateRtsControlState();
    updateTimedSendTimer();
    appendLog(uiString(T::SystemReconnectOkPrefix) + utf8ToWide(openResult.endpoint));
    setStatus(tx(T::AutoReconnectOk));
}

} // namespace svm::win32

#endif
