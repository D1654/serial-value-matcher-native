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

void NativeMainWindow::pollSerial() {
    if (!serialPort_.isOpen()) {
        return;
    }
    if (!serialIoState_.allowsSerialPoll()) {
        return;
    }
    if (!serialPort_.waitForReadyRead(0)) {
        if (!serialPort_.lastErrorText().empty()) {
            handleSerialFailure(serialPort_.lastErrorText());
        }
        return;
    }

    std::vector<native_storage::RawIoEvent> events;
    std::vector<std::uint8_t> mergedPayload;
    const std::string endpoint = serialPort_.endpoint();
    for (int batch = 0; batch < 8; ++batch) {
        const std::vector<std::uint8_t> payload = serialPort_.readAvailable(4096);
        if (payload.empty()) {
            if (!serialPort_.lastErrorText().empty()) {
                handleSerialFailure(serialPort_.lastErrorText());
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
    if (!serialPort_.isOpen()) {
        return;
    }
    const std::string endpoint = serialPort_.endpoint();
    stopFileSend({});
    serialPort_.close();
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
    if (!reconnectState_.shouldTryReconnect(serialPort_.isOpen())) {
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
    if (!serialPort_.open(*options)) {
        setStatus(uiString(T::AutoReconnectFailedPrefix) + utf8ToWide(serialPort_.lastErrorText()));
        reconnectState_.markReconnectFailed();
        KillTimer(window_, IDT_RECONNECT);
        return;
    }

    reconnectState_.markReconnectSucceeded();
    KillTimer(window_, IDT_RECONNECT);
    updateConnectionButtonState();
    updateRtsControlState();
    appendLog(uiString(T::SystemReconnectOkPrefix) + utf8ToWide(serialPort_.endpoint()));
    setStatus(tx(T::AutoReconnectOk));
}

} // namespace svm::win32

#endif
