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

std::wstring writeResultMessage(const svm::transport::SerialWriteResult& result) {
    if (!result.message.empty()) {
        return utf8ToWide(result.message);
    }
    switch (result.status) {
    case svm::transport::SerialWriteResultStatus::Timeout:
        return L"串口写入超时。";
    case svm::transport::SerialWriteResultStatus::Cancelled:
        return L"串口写入已取消。";
    case svm::transport::SerialWriteResultStatus::Failed:
        return L"串口写入失败。";
    case svm::transport::SerialWriteResultStatus::RejectedFull:
        return L"串口写入队列已满。";
    case svm::transport::SerialWriteResultStatus::RejectedInvalid:
        return L"串口写入请求无效。";
    case svm::transport::SerialWriteResultStatus::Accepted:
        return L"串口写入请求已接受。";
    case svm::transport::SerialWriteResultStatus::Sent:
        return {};
    }
    return L"串口写入状态未知。";
}

} // namespace

void NativeMainWindow::updateSerialWriteQueueStatus() {
    serialIoState_.updateWriteQueueStatus(serialTransport_.writeQueueSnapshot());
}

void NativeMainWindow::clearPendingSerialWrites() {
    pendingSerialWrites_.clear();
    serialTransport_.takeCompletedWrites();
    updateSerialWriteQueueStatus();
}

void NativeMainWindow::drainSerialWriteResults() {
    const std::vector<svm::transport::SerialWriteResult> results = serialTransport_.takeCompletedWrites();
    if (results.empty()) {
        updateSerialWriteQueueStatus();
        return;
    }

    for (const auto& result : results) {
        const auto found = std::find_if(
            pendingSerialWrites_.begin(),
            pendingSerialWrites_.end(),
            [&result](const NativePendingSerialWrite& pending) {
                return pending.requestId == result.requestId;
            });
        if (found == pendingSerialWrites_.end()) {
            continue;
        }

        NativePendingSerialWrite pending = std::move(*found);
        pendingSerialWrites_.erase(found);

        if (result.status == svm::transport::SerialWriteResultStatus::Cancelled) {
            setStatus(writeResultMessage(result));
            continue;
        }

        if (result.status != svm::transport::SerialWriteResultStatus::Sent) {
            const std::wstring message = writeResultMessage(result);
            if (pending.source == NativePendingSerialWriteSource::File && fileSend_.active()) {
                stopFileSend(message);
            } else {
                setStatus(message);
            }
            handleSerialFailure(result.message.empty() ? "串口写入失败。" : result.message);
            continue;
        }

        if (pending.source == NativePendingSerialWriteSource::Manual) {
            saveRawEvent("Tx", pending.payload);
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

        if (!fileSend_.active()) {
            continue;
        }

        saveRawEvent("Tx", pending.payload);
        appendPayloadLog(NativeLogKind::Tx, pending.payload);
        fileSend_.markBytesWritten(result.byteCount);
        statusCountersState_.addTxBytes(static_cast<std::uint64_t>(result.byteCount));
        updateFileSendProgress();
        updateStatusSegments();

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
    if (!serialTransport_.isOpen()) {
        return;
    }
    if (!serialIoState_.allowsSerialPoll()) {
        return;
    }
    if (!serialTransport_.waitForReadyRead(0)) {
        if (!serialTransport_.lastErrorText().empty()) {
            handleSerialFailure(serialTransport_.lastErrorText());
        }
        return;
    }

    std::vector<native_storage::RawIoEvent> events;
    std::vector<std::uint8_t> mergedPayload;
    const std::string endpoint = serialTransport_.endpoint();
    for (int batch = 0; batch < 8; ++batch) {
        const std::vector<std::uint8_t> payload = serialTransport_.readAvailable(4096);
        if (payload.empty()) {
            if (!serialTransport_.lastErrorText().empty()) {
                handleSerialFailure(serialTransport_.lastErrorText());
            }
            break;
        }
        native_storage::RawIoEvent event;
        event.sessionId = sessionId_;
        event.direction = "Rx";
        event.timestampUtc = nativeUtcTimestampText();
        event.endpoint = endpoint;
        event.payload = payload;
        events.push_back(std::move(event));
        mergedPayload.insert(mergedPayload.end(), payload.begin(), payload.end());
    }
    if (mergedPayload.empty()) {
        return;
    }
    saveRawEvents(std::move(events));
    appendPayloadLog(NativeLogKind::Rx, mergedPayload);
    statusCountersState_.addRxBytes(static_cast<std::uint64_t>(mergedPayload.size()));
    updateStatusSegments();
}

void NativeMainWindow::handleSerialFailure(const std::string& message) {
    if (!serialTransport_.isOpen()) {
        return;
    }
    const std::string endpoint = serialTransport_.endpoint();
    stopFileSend({});
    serialTransport_.close();
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
    if (!reconnectState_.shouldTryReconnect(serialTransport_.isOpen())) {
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
    if (!serialTransport_.open(*options)) {
        setStatus(uiString(T::AutoReconnectFailedPrefix) + utf8ToWide(serialTransport_.lastErrorText()));
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    reconnectState_.markReconnectSucceeded();
    KillTimer(window_, IDT_RECONNECT);
    updateConnectionButtonState();
    updateRtsControlState();
    appendLog(uiString(T::SystemReconnectOkPrefix) + utf8ToWide(serialTransport_.endpoint()));
    setStatus(tx(T::AutoReconnectOk));
}

} // namespace svm::win32

#endif
