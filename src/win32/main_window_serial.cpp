#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_connection_ui_state.h"
#include "win32/native_control_utils.h"
#include "win32/native_log_model.h"
#include "win32/native_serial_profile_codec.h"
#include "win32/native_time_utils.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"
#include "win32/win32_serial_enumerator.h"
#include "win32/win32_serial_types.h"

#include <algorithm>
#include <string>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

std::wstring portDisplayText(const SerialPortDescriptor& port) {
    const std::wstring portName = utf8ToWide(port.portName);
    const std::wstring description = sanitizeLogText(utf8ToWide(port.description));
    if (description.empty() || description == portName) {
        return portName;
    }
    return portName + L" - " + description;
}

} // namespace

void NativeMainWindow::refreshPorts() {
    const std::string currentPort = selectedPortName();
    SendMessageW(portCombo_, CB_RESETCONTENT, 0, 0);
    availablePorts_ = Win32SerialEnumerator::availablePorts();
    for (std::size_t index = 0; index < availablePorts_.size(); ++index) {
        const std::wstring display = portDisplayText(availablePorts_[index]);
        const LRESULT item = SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
        if (item >= 0) {
            SendMessageW(portCombo_, CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(index + 1));
        }
    }
    if (!availablePorts_.empty()) {
        LRESULT preserved = -1;
        for (std::size_t index = 0; index < availablePorts_.size(); ++index) {
            if (normalizedComPortName(availablePorts_[index].portName) == normalizedComPortName(currentPort)) {
                preserved = static_cast<LRESULT>(index);
                break;
            }
        }
        SendMessageW(portCombo_, CB_SETCURSEL, preserved >= 0 ? static_cast<WPARAM>(preserved) : 0, 0);
        setStatus(uiString(T::RefreshedPortsPrefix) + std::to_wstring(availablePorts_.size()) + uiString(T::PortsUnitSuffix));
    } else {
        setStatus(tx(T::NoPortsStatus));
    }
}

void NativeMainWindow::applyLatestSerialProfile() {
    if (!store_.isOpen()) {
        return;
    }
    const auto profile = store_.latestSerialProfile();
    if (!profile.has_value()) {
        return;
    }

    const std::string normalizedProfilePort = normalizedComPortName(profile->portName);
    const std::wstring portName = utf8ToWide(normalizedProfilePort.empty() ? profile->portName : normalizedProfilePort);
    LRESULT portIndex = -1;
    for (std::size_t index = 0; index < availablePorts_.size(); ++index) {
        if (normalizedComPortName(availablePorts_[index].portName) == normalizedProfilePort) {
            portIndex = static_cast<LRESULT>(index);
            break;
        }
    }
    if (portIndex < 0 && !portName.empty()) {
        portIndex = SendMessageW(portCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(portName.c_str()));
        if (portIndex >= 0) {
            SendMessageW(portCombo_, CB_SETITEMDATA, static_cast<WPARAM>(portIndex), 0);
        }
    }
    if (portIndex >= 0) {
        SendMessageW(portCombo_, CB_SETCURSEL, static_cast<WPARAM>(portIndex), 0);
    }

    setControlText(baudCombo_, std::to_wstring(profile->baudRate));
    selectComboData(dataBitsCombo_, profile->dataBits);
    selectComboData(parityCombo_, static_cast<LPARAM>(nativeSerialParityFromKey(profile->parity)));
    selectComboData(stopBitsCombo_, static_cast<LPARAM>(nativeSerialStopBitsFromKey(profile->stopBits)));
    selectComboData(flowControlCombo_, static_cast<LPARAM>(nativeSerialFlowControlFromKey(profile->flowControl)));
    SendMessageW(dtrCheck_, BM_SETCHECK, profile->dataTerminalReady ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(rtsCheck_, BM_SETCHECK, profile->requestToSend ? BST_CHECKED : BST_UNCHECKED, 0);
    updateRtsControlState();
    setStatus(uiString(T::RestoredProfilePrefix) + portName + uiString(T::ChinesePeriod));
}

void NativeMainWindow::saveCurrentSerialProfile() {
    if (!store_.isOpen()) {
        setStatus(tx(T::StorageSaveProfileClosed));
        return;
    }
    const SerialOpenOptions options = currentOpenOptions();
    native_storage::SerialProfile profile;
    profile.name = "default";
    profile.portName = options.portName;
    profile.baudRate = options.baudRate;
    profile.dataBits = options.dataBits;
    profile.parity = nativeSerialParityKey(options.parity);
    profile.stopBits = nativeSerialStopBitsKey(options.stopBits);
    profile.flowControl = nativeSerialFlowControlKey(options.flowControl);
    profile.dataTerminalReady = options.dataTerminalReady;
    profile.requestToSend = options.requestToSend;
    profile.updatedAtUtc = nativeUtcTimestampText();
    if (!store_.saveSerialProfile(profile)) {
        setStatus(utf8ToWide(store_.lastErrorText()));
        return;
    }
    setStatus(uiString(T::SavedProfilePrefix) + utf8ToWide(options.portName) + L"\uFF0C" + std::to_wstring(options.baudRate) + uiString(T::ChinesePeriod));
}

void NativeMainWindow::toggleConnection() {
    if (serialPort_.isOpen()) {
        disconnectSerial();
        return;
    }
    connectSerial();
}

void NativeMainWindow::connectSerial() {
    if (serialPort_.isOpen()) {
        setStatus(tx(T::AlreadyConnected));
        return;
    }

    SerialOpenOptions options = currentOpenOptions();
    const auto validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        setStatus(utf8ToWide(validation.errorMessage));
        return;
    }

    if (!serialPort_.open(options)) {
        setStatus(utf8ToWide(serialPort_.lastErrorText()));
        return;
    }

    reconnectState_.rememberSuccessfulOpen(options);
    disconnectAfterModbusScan_ = false;
    KillTimer(window_, IDT_RECONNECT);
    appendLog(uiString(T::SystemConnectedPrefix) + utf8ToWide(serialPort_.endpoint()));
    updateConnectionButtonState();
    updateRtsControlState();
    saveCurrentSerialProfile();
    updateTimedSendTimer();
    setStatus(tx(T::ConnectedStatus));
}

void NativeMainWindow::disconnectSerial() {
    if (serialIoState_.shouldDeferDisconnect()) {
        disconnectAfterModbusScan_ = true;
        requestCancelModbusScan();
        setStatus(tx(T::ModbusDisconnectPendingStatus));
        return;
    }
    closeSerialPort(tx(T::DisconnectedStatus));
}

void NativeMainWindow::closeSerialPort(const std::wstring& statusText) {
    if (!serialPort_.isOpen()) {
        return;
    }
    KillTimer(window_, IDT_TIMED_SEND);
    stopFileSend({});
    const std::wstring endpoint = utf8ToWide(serialPort_.endpoint());
    serialPort_.close();
    clearPendingSerialWrites();
    appendLog(uiString(T::SystemDisconnectedPrefix) + endpoint);
    updateConnectionButtonState();
    updateRtsControlState();
    updateTimedSendTimer();
    setStatus(statusText.empty() ? tx(T::DisconnectedStatus) : statusText.c_str());
}

void NativeMainWindow::shutdownSerialPort() {
    if (serialPort_.isOpen()) {
        serialPort_.close();
    }
    clearPendingSerialWrites();
}

std::wstring NativeMainWindow::serialIoBusyStatus() const {
    if (serialIoState_.hasPendingSerialWrites()) {
        return L"串口写入队列仍有待发送数据，请稍后再试。";
    }
    if (!serialIoState_.isBusy()) {
        return tx(T::SerialIoBusyStatus);
    }
    switch (serialIoState_.owner()) {
    case NativeSerialIoOwner::FileSend:
        return tx(T::FileSendBusyStatus);
    case NativeSerialIoOwner::ModbusScan:
        return tx(T::ModbusRunning);
    case NativeSerialIoOwner::ManualSend:
    case NativeSerialIoOwner::None:
        break;
    }
    return tx(T::SerialIoBusyStatus);
}

void NativeMainWindow::releaseModbusScanOwnership() {
    modbusScanRunning_ = false;
    serialIoState_.release(NativeSerialIoOwner::ModbusScan);
}

std::string NativeMainWindow::selectedPortName() const {
    const LRESULT index = SendMessageW(portCombo_, CB_GETCURSEL, 0, 0);
    if (index >= 0) {
        const LRESULT itemData = SendMessageW(portCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
        if (itemData > 0) {
            const std::size_t portIndex = static_cast<std::size_t>(itemData - 1);
            if (portIndex < availablePorts_.size()) {
                return availablePorts_[portIndex].portName;
            }
        }
    }

    std::wstring text = controlText(portCombo_);
    const std::wstring separator = L" - ";
    const std::size_t separatorPosition = text.find(separator);
    if (separatorPosition != std::wstring::npos) {
        text.resize(separatorPosition);
    }
    return wideToUtf8(text);
}

SerialOpenOptions NativeMainWindow::currentOpenOptions() const {
    SerialOpenOptions options;
    options.portName = selectedPortName();
    options.baudRate = std::max(1, textToInt(baudCombo_, 115200));
    options.dataBits = static_cast<int>(selectedComboData(dataBitsCombo_, 8));
    options.parity = static_cast<SerialParity>(selectedComboData(parityCombo_, static_cast<LPARAM>(SerialParity::None)));
    options.stopBits = static_cast<SerialStopBits>(selectedComboData(stopBitsCombo_, static_cast<LPARAM>(SerialStopBits::One)));
    options.flowControl = static_cast<SerialFlowControl>(selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None)));
    options.dataTerminalReady = SendMessageW(dtrCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.requestToSend = SendMessageW(rtsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return options;
}

void NativeMainWindow::applySerialLineControl(WORD controlId) {
    const bool enabled = SendMessageW(controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!serialIoState_.allowsLineControl()) {
        HWND control = controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_;
        SendMessageW(control, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
        setStatus(serialIoBusyStatus());
        return;
    }
    if (!serialPort_.isOpen()) {
        setStatus(std::wstring(controlId == IDC_DTR_CHECK ? tx(T::DtrAppliedPrefix) : tx(T::RtsAppliedPrefix))
            + (enabled ? tx(T::SignalEnabledSuffix) : tx(T::SignalDisabledSuffix))
            + L" \u4E0B\u6B21\u8FDE\u63A5\u751F\u6548\u3002");
        return;
    }

    bool ok = false;
    if (controlId == IDC_DTR_CHECK) {
        ok = serialPort_.setDataTerminalReady(enabled);
        if (ok) {
            reconnectState_.updateDataTerminalReady(enabled);
        }
    } else {
        ok = serialPort_.setRequestToSend(enabled);
        if (ok) {
            reconnectState_.updateRequestToSend(enabled);
        }
    }

    if (!ok) {
        HWND control = controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_;
        SendMessageW(control, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
        setStatus(utf8ToWide(serialPort_.lastErrorText()));
        updateRtsControlState();
        return;
    }

    setStatus(std::wstring(controlId == IDC_DTR_CHECK ? tx(T::DtrAppliedPrefix) : tx(T::RtsAppliedPrefix))
        + (enabled ? tx(T::SignalEnabledSuffix) : tx(T::SignalDisabledSuffix)));
}

void NativeMainWindow::updateConnectionButtonState() {
    const NativeConnectionButtonMode mode = connectionUiState_.buttonMode(serialPort_.isOpen());
    setControlText(connectButton_, mode == NativeConnectionButtonMode::Disconnect ? tx(T::DisconnectButton) : tx(T::ConnectButton));
}

void NativeMainWindow::updateRtsControlState() {
    const bool selectedHardwareRtsCts = selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None))
        == static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts);
    const NativeLineControlUiState lineControlState = connectionUiState_.lineControlState(
        serialIoState_.allowsLineControl(),
        serialPort_.isOpen(),
        selectedHardwareRtsCts,
        serialPort_.isOpen() && serialPort_.usesHardwareRtsCts());
    EnableWindow(rtsCheck_, lineControlState.rtsEnabled ? TRUE : FALSE);
}

} // namespace svm::win32

#endif
