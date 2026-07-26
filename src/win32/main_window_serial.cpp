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
    SerialOpenOptions options = currentOpenOptions();
    const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
    if (snapshot.open()) {
        options = snapshot.options;
    }
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
    if (serialLifecycle_.snapshot().open()) {
        disconnectSerial();
        return;
    }
    connectSerial();
}

void NativeMainWindow::connectSerial() {
    const svm::transport::SerialSessionSnapshot currentSession = serialLifecycle_.snapshot();
    if (currentSession.open()) {
        setStatus(tx(T::AlreadyConnected));
        return;
    }
    if (serialIoState_.isOwnedBy(NativeSerialIoOwner::ModbusScan)) {
        setStatus(tx(T::ModbusRunning));
        return;
    }
    if (currentSession.state != svm::transport::SerialSessionState::Closed) {
        closeSerialPort({});
    }

    SerialOpenOptions options = currentOpenOptions();
    const auto validation = validateSerialOpenOptions(options);
    if (!validation.ok) {
        setStatus(utf8ToWide(validation.errorMessage));
        return;
    }

    const svm::transport::SerialOperationResult openResult = serialLifecycle_.open(options);
    if (!openResult.succeeded()) {
        const std::wstring message = serialOperationErrorMessage(openResult, "打开串口");
        appendSerialOperationLog(NativeLogKind::Error, message, openResult);
        setStatus(message);
        return;
    }

    const svm::transport::SerialSessionSnapshot openedSession = serialLifecycle_.snapshot();
    if (!openedSession.open() || openedSession.generation != openResult.operation.generation) {
        const svm::transport::SerialOperationResult closeResult = serialLifecycle_.close();
        appendSerialOperationLog(
            closeResult.succeeded() ? NativeLogKind::System : NativeLogKind::Error,
            closeResult.succeeded()
                ? std::wstring(L"已关闭未稳定发布的串口会话。")
                : serialOperationErrorMessage(closeResult, "关闭未稳定串口会话"),
            closeResult);
        setStatus(L"串口连接状态未能稳定发布，请重试。");
        return;
    }

    reconnectState_.rememberSuccessfulOpen(openedSession.options);
    disconnectAfterModbusScan_ = false;
    timedSendConfirmed_ = false;
    KillTimer(window_, IDT_RECONNECT);
    appendSerialOperationLog(
        NativeLogKind::System,
        uiString(T::SystemConnectedPrefix) + utf8ToWide(openResult.endpoint),
        openResult);
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
    const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
    KillTimer(window_, IDT_TIMED_SEND);
    KillTimer(window_, IDT_RECONNECT);
    reconnectState_.clearWaiting();
    timedSendConfirmed_ = false;
    stopFileSend({}, true);
    drainSerialWriteResults();
    KillTimer(window_, IDT_RECONNECT);
    reconnectState_.clearWaiting();
    const svm::transport::SerialSessionSnapshot closingSession = serialLifecycle_.snapshot();
    std::optional<svm::transport::SerialOperationResult> closeResult;
    if (closingSession.state != svm::transport::SerialSessionState::Closed) {
        closeResult = serialLifecycle_.close();
    }
    settleClosingSerialWrites(closingSession.generation, true);
    if (closeResult.has_value()) {
        const std::wstring message = closeResult->succeeded()
            ? uiString(T::SystemDisconnectedPrefix) + utf8ToWide(snapshot.endpoint)
            : serialOperationErrorMessage(*closeResult, "关闭串口");
        appendSerialOperationLog(
            closeResult->succeeded() ? NativeLogKind::System : NativeLogKind::Error,
            message,
            *closeResult);
        if (!closeResult->succeeded()) {
            updateConnectionButtonState();
            updateRtsControlState();
            updateTimedSendTimer();
            setStatus(message);
            return;
        }
    }
    updateConnectionButtonState();
    updateRtsControlState();
    updateTimedSendTimer();
    setStatus(statusText.empty() ? tx(T::DisconnectedStatus) : statusText.c_str());
}

void NativeMainWindow::shutdownSerialPort() {
    KillTimer(window_, IDT_TIMED_SEND);
    KillTimer(window_, IDT_RECONNECT);
    reconnectState_.clearWaiting();
    drainSerialWriteResults();
    KillTimer(window_, IDT_RECONNECT);
    reconnectState_.clearWaiting();
    const svm::transport::SerialSessionSnapshot closingSession = serialLifecycle_.snapshot();
    std::optional<svm::transport::SerialOperationResult> closeResult;
    if (closingSession.state != svm::transport::SerialSessionState::Closed) {
        closeResult = serialLifecycle_.close();
    }
    timedSendConfirmed_ = false;
    settleClosingSerialWrites(closingSession.generation, false);
    if (closeResult.has_value()) {
        saveRawEvent(*closeResult, {});
    }
}

std::wstring NativeMainWindow::serialIoBusyStatus() const {
    if (serialIoState_.hasPendingSerialWrites()) {
        return L"串口写入队列仍有待发送数据（"
            + serialWriteQueuePressureStatus()
            + L"），请稍后再试。";
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

std::wstring NativeMainWindow::serialWriteQueuePressureStatus() const {
    const auto& snapshot = serialIoState_.writeQueueSnapshot();
    if (!snapshot.has_value()) {
        return {};
    }
    return L"请求 "
        + std::to_wstring(snapshot->countedCount())
        + L"/"
        + std::to_wstring(snapshot->capacity)
        + L"，字节 "
        + std::to_wstring(snapshot->countedBytes())
        + L"/"
        + std::to_wstring(snapshot->byteCapacity);
}

void NativeMainWindow::releaseModbusScanOwnership() {
    modbusScanRunning_ = false;
    modbusScanGeneration_ = svm::transport::kUnassignedSerialSessionGeneration;
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

std::wstring NativeMainWindow::serialOperationErrorMessage(
    const svm::transport::SerialOperationResult& result,
    std::string_view operation) const {
    switch (result.status) {
    case svm::transport::SerialOperationStatus::Accepted:
        return L"串口操作已接受。";
    case svm::transport::SerialOperationStatus::Succeeded:
        return {};
    case svm::transport::SerialOperationStatus::RejectedInvalid:
        return L"串口参数或请求无效。";
    case svm::transport::SerialOperationStatus::RejectedFull: {
        const std::wstring pressure = serialWriteQueuePressureStatus();
        return pressure.empty()
            ? std::wstring(L"串口写入请求因队列达到上限被拒绝。")
            : L"串口写入请求因队列达到上限被拒绝（当前压力：" + pressure + L"）。";
    }
    case svm::transport::SerialOperationStatus::RejectedClosed:
        return L"串口会话已关闭。";
    case svm::transport::SerialOperationStatus::Timeout:
        return L"串口操作超时。";
    case svm::transport::SerialOperationStatus::Cancelled:
        return L"串口操作已取消。";
    case svm::transport::SerialOperationStatus::Disconnected:
        return L"串口设备已断开。";
    case svm::transport::SerialOperationStatus::Failed:
        break;
    }
    if (result.error.nativeCode != 0) {
        return utf8ToWide(win32SerialErrorText(result.error.nativeCode, operation));
    }
    if (result.byteCount > 0) {
        return L"串口操作在处理 " + std::to_wstring(result.byteCount) + L" 字节后失败。";
    }
    switch (result.error.category) {
    case svm::transport::SerialErrorCategory::InvalidInput:
        return L"串口参数无效。";
    case svm::transport::SerialErrorCategory::SessionClosed:
        return L"串口会话已关闭。";
    case svm::transport::SerialErrorCategory::QueueFull:
        return L"串口写入队列已满。";
    case svm::transport::SerialErrorCategory::Timeout:
        return L"串口操作超时。";
    case svm::transport::SerialErrorCategory::Cancelled:
        return L"串口操作已取消。";
    case svm::transport::SerialErrorCategory::Disconnected:
        return L"串口设备已断开。";
    case svm::transport::SerialErrorCategory::NativeFailure:
        return L"串口原生操作失败。";
    case svm::transport::SerialErrorCategory::IoFailure:
        return L"串口 I/O 操作失败。";
    case svm::transport::SerialErrorCategory::None:
        break;
    }
    return L"串口操作失败。";
}

void NativeMainWindow::applySerialLineControl(WORD controlId) {
    const bool enabled = SendMessageW(controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!serialIoState_.allowsLineControl()) {
        HWND control = controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_;
        SendMessageW(control, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
        setStatus(serialIoBusyStatus());
        return;
    }
    const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
    if (!snapshot.open()) {
        setStatus(std::wstring(controlId == IDC_DTR_CHECK ? tx(T::DtrAppliedPrefix) : tx(T::RtsAppliedPrefix))
            + (enabled ? tx(T::SignalEnabledSuffix) : tx(T::SignalDisabledSuffix))
            + L" \u4E0B\u6B21\u8FDE\u63A5\u751F\u6548\u3002");
        return;
    }

    const bool dataTerminalReady = controlId == IDC_DTR_CHECK;
    const svm::transport::SerialOperationResult result = dataTerminalReady
        ? serialLifecycle_.setDataTerminalReady(enabled)
        : serialLifecycle_.setRequestToSend(enabled);
    if (result.succeeded()) {
        if (dataTerminalReady) {
            reconnectState_.updateDataTerminalReady(enabled);
        } else {
            reconnectState_.updateRequestToSend(enabled);
        }
    }

    if (!result.succeeded()) {
        HWND control = controlId == IDC_DTR_CHECK ? dtrCheck_ : rtsCheck_;
        SendMessageW(control, BM_SETCHECK, enabled ? BST_UNCHECKED : BST_CHECKED, 0);
        updateRtsControlState();
        std::wstring message;
        if (!dataTerminalReady
            && result.status == svm::transport::SerialOperationStatus::RejectedInvalid
            && snapshot.usesHardwareRtsCts()) {
            message = uiString(T::RtsHardwareManaged);
        } else {
            message = serialOperationErrorMessage(
                result,
                dataTerminalReady ? "设置 DTR 信号" : "设置 RTS 信号");
        }
        appendSerialOperationLog(NativeLogKind::Error, message, result);
        if (result.status == svm::transport::SerialOperationStatus::Disconnected) {
            handleSerialFailure(wideToUtf8(message), false);
        } else {
            setStatus(message);
        }
        return;
    }

    const std::wstring message = std::wstring(
        controlId == IDC_DTR_CHECK ? tx(T::DtrAppliedPrefix) : tx(T::RtsAppliedPrefix))
        + (enabled ? tx(T::SignalEnabledSuffix) : tx(T::SignalDisabledSuffix));
    appendSerialOperationLog(NativeLogKind::System, message, result);
    setStatus(message);
}

void NativeMainWindow::updateConnectionButtonState() {
    const NativeConnectionButtonMode mode = connectionUiState_.buttonMode(serialLifecycle_.snapshot().open());
    setControlText(connectButton_, mode == NativeConnectionButtonMode::Disconnect ? tx(T::DisconnectButton) : tx(T::ConnectButton));
}

void NativeMainWindow::updateRtsControlState() {
    const bool selectedHardwareRtsCts = selectedComboData(flowControlCombo_, static_cast<LPARAM>(SerialFlowControl::None))
        == static_cast<LPARAM>(SerialFlowControl::HardwareRtsCts);
    const svm::transport::SerialSessionSnapshot snapshot = serialLifecycle_.snapshot();
    const NativeLineControlUiState lineControlState = connectionUiState_.lineControlState(
        serialIoState_.allowsLineControl(),
        snapshot.open(),
        selectedHardwareRtsCts,
        snapshot.open() && snapshot.usesHardwareRtsCts());
    EnableWindow(rtsCheck_, lineControlState.rtsEnabled ? TRUE : FALSE);
}

} // namespace svm::win32

#endif
