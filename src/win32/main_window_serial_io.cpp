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

} // namespace

void NativeMainWindow::updateSerialWriteQueueStatus() {
    serialIoState_.updateWriteQueueSnapshot(serialWriteScheduler_.writeQueueSnapshot());
}

void NativeMainWindow::settleClosingSerialWrites(
    svm::transport::SerialSessionGeneration closingGeneration,
    bool emitUiEvidence) {
    for (const svm::transport::SerialTerminalResult& result : serialWriteScheduler_.takeCompletedWrites()) {
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
        if (nativeSerialWriteShouldPublishOnClose(pending.key, result, closingGeneration)) {
            publishSuccessfulSerialWrite(result, pending, emitUiEvidence);
        }
    }
    pendingSerialWrites_.clear();
    updateSerialWriteQueueStatus();
}

void NativeMainWindow::publishSuccessfulSerialWrite(
    const svm::transport::SerialTerminalResult& result,
    const NativePendingSerialWrite& pending,
    bool emitUiEvidence) {
    saveRawEvent(result, pending.payload);
    if (emitUiEvidence) {
        appendPayloadLog(NativeLogKind::Tx, pending.payload, result);
    }
    statusCountersState_.addTxBytes(static_cast<std::uint64_t>(result.byteCount));
    if (emitUiEvidence) {
        updateStatusSegments();
    }

    if (pending.source != NativePendingSerialWriteSource::Manual || !pending.saveHistory) {
        return;
    }
    native_storage::SendHistoryEntry history = nativeMakeSendHistoryEntry(
        wideToUtf8(pending.manualText),
        pending.payloadMode,
        pending.lineEnding,
        pending.textCodePage,
        nativeUtcTimestampText());
    if (!store_.isOpen() || !store_.saveSendHistory(history)) {
        reportStorageFailure(L"保存发送历史");
    } else if (emitUiEvidence) {
        refreshSendHistory();
    }
}

void NativeMainWindow::drainSerialWriteResults() {
    const std::vector<svm::transport::SerialTerminalResult> results =
        serialWriteScheduler_.takeCompletedWrites();
    if (results.empty()) {
        updateSerialWriteQueueStatus();
        return;
    }

    std::optional<std::wstring> deferredFailureMessage;
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
            const std::wstring message = serialOperationErrorMessage(result, "写入串口");
            appendSerialOperationLog(NativeLogKind::System, message, result);
            setStatus(message);
            continue;
        }

        if (decision == NativeSerialWriteCompletionDecision::Failed) {
            const std::wstring message = serialOperationErrorMessage(result, "写入串口");
            appendSerialOperationLog(NativeLogKind::Error, message, result);
            setStatus(message);
            if (!deferredFailureMessage.has_value()) {
                deferredFailureMessage = message;
            }
            continue;
        }

        publishSuccessfulSerialWrite(result, pending, true);

        if (pending.source == NativePendingSerialWriteSource::Manual) {
            setStatus(uiString(T::SentPrefix) + std::to_wstring(result.byteCount) + uiString(T::BytesSuffix));
            continue;
        }

        if (!fileSend_.active()
            || fileSendGeneration_ != result.operation.generation) {
            continue;
        }

        fileSend_.markBytesWritten(result.byteCount);
        updateFileSendProgress();

        if (fileSend_.done()) {
            const std::wstring summary = uiString(T::FileSendDonePrefix) + std::to_wstring(fileSend_.sentBytes()) + uiString(T::BytesSuffix);
            stopFileSend(summary, true);
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
    if (deferredFailureMessage.has_value()) {
        handleSerialFailure(wideToUtf8(*deferredFailureMessage), false);
    }
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
    std::vector<NativeLogEntry> payloadEntries;
    std::size_t receivedBytes = 0;
    std::optional<svm::transport::SerialOperationResult> failureResult;
    std::optional<svm::transport::SerialOperationResult> statusResult;
    for (int batch = 0; batch < 8; ++batch) {
        svm::transport::SerialReadResult read = serialByteStream_.readAvailable(4096);
        const NativeSerialReadDecision decision =
            nativeSerialReadDecision(read, polledSession.generation);
        if (decision == NativeSerialReadDecision::Stop) {
            break;
        }
        if (decision == NativeSerialReadDecision::Fail) {
            failureResult = read.operation;
            break;
        }
        if (decision == NativeSerialReadDecision::ReportError) {
            statusResult = read.operation;
            break;
        }
        receivedBytes += read.bytes.size();
        events.push_back(makeRawSerialEvent(read.operation, read.bytes));
        payloadEntries.push_back(nativeMakeSerialPayloadLogEntry(
            NativeLogKind::Rx,
            nativeLocalClockText(),
            std::move(read.bytes),
            read.operation));
    }

    const svm::transport::SerialSessionSnapshot currentSession = serialLifecycle_.snapshot();
    if (currentSession.open() && currentSession.generation != polledSession.generation) {
        return;
    }
    if (!events.empty()) {
        saveRawEvents(std::move(events));
        for (NativeLogEntry& entry : payloadEntries) {
            addLogEntry(std::move(entry));
        }
        statusCountersState_.addRxBytes(static_cast<std::uint64_t>(receivedBytes));
        updateStatusSegments();
    }
    if (failureResult.has_value()) {
        const std::wstring message = serialOperationErrorMessage(*failureResult, "读取串口");
        appendSerialOperationLog(NativeLogKind::Error, message, *failureResult);
        handleSerialFailure(wideToUtf8(message), false);
    } else if (statusResult.has_value()) {
        const std::wstring message = serialOperationErrorMessage(*statusResult, "读取串口");
        appendSerialOperationLog(NativeLogKind::Error, message, *statusResult);
        setStatus(message);
    }
}

void NativeMainWindow::handleSerialFailure(const std::string& message, bool appendLogEntry) {
    const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
    if (snapshot.state == svm::transport::SerialSessionState::Closed) {
        return;
    }
    const std::string endpoint = snapshot.endpoint;
    KillTimer(window_, IDT_TIMED_SEND);
    timedSendConfirmed_ = false;
    stopFileSend({}, true);
    const svm::transport::SerialOperationResult closeResult = serialLifecycle_.close();
    settleClosingSerialWrites(snapshot.generation, true);
    appendSerialOperationLog(
        closeResult.succeeded() ? NativeLogKind::System : NativeLogKind::Error,
        closeResult.succeeded()
            ? std::wstring(L"串口故障会话已关闭。")
            : serialOperationErrorMessage(closeResult, "关闭故障串口会话"),
        closeResult);
    updateConnectionButtonState();
    updateRtsControlState();
    if (appendLogEntry) {
        appendLog(NativeLogKind::Error, uiString(T::SystemSerialFailedPrefix) + utf8ToWide(message));
    }
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
        const std::wstring message = uiString(T::AutoReconnectFailedPrefix)
            + serialOperationErrorMessage(openResult, "自动重连串口");
        appendSerialOperationLog(NativeLogKind::Error, message, openResult);
        setStatus(message);
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    const svm::transport::SerialSessionSnapshot openedSession = serialLifecycle_.snapshot();
    if (!openedSession.open() || openedSession.generation != openResult.operation.generation) {
        const svm::transport::SerialOperationResult closeResult = serialLifecycle_.close();
        appendSerialOperationLog(
            closeResult.succeeded() ? NativeLogKind::System : NativeLogKind::Error,
            closeResult.succeeded()
                ? std::wstring(L"已关闭未稳定发布的自动重连会话。")
                : serialOperationErrorMessage(closeResult, "关闭自动重连会话"),
            closeResult);
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
    appendSerialOperationLog(
        NativeLogKind::System,
        uiString(T::SystemReconnectOkPrefix) + utf8ToWide(openResult.endpoint),
        openResult);
    setStatus(tx(T::AutoReconnectOk));
}

} // namespace svm::win32

#endif
