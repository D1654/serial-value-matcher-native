#include "win32/main_window.h"

#if defined(_WIN32)

#include "core/modbus_core.h"

#include "win32/native_control_utils.h"
#include "win32/native_modbus_scan_request.h"
#include "win32/native_modbus_scan_ui_state.h"
#include "win32/native_modbus_scan_worker.h"
#include "win32/native_time_utils.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"

#include <algorithm>
#include <commctrl.h>
#include <memory>
#include <string>
#include <utility>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

void NativeMainWindow::updateModbusScanProgress(
    std::size_t completedBlocks,
    std::size_t totalBlocks,
    std::size_t successBlocks,
    std::size_t failedBlocks,
    std::size_t observations) {
    if (modbusProgress_ != nullptr) {
        SendMessageW(modbusProgress_, PBM_SETRANGE32, 0, 1000);
        const int position = totalBlocks == 0
            ? 0
            : static_cast<int>(std::min<std::size_t>(1000, (completedBlocks * 1000) / totalBlocks));
        SendMessageW(modbusProgress_, PBM_SETPOS, static_cast<WPARAM>(position), 0);
    }

    const std::wstring text = std::to_wstring(completedBlocks)
        + L"/"
        + std::to_wstring(totalBlocks)
        + L"  \u6210"
        + std::to_wstring(successBlocks)
        + L" \u5931"
        + std::to_wstring(failedBlocks)
        + L" \u89C2"
        + std::to_wstring(observations);
    if (modbusProgressText_ != nullptr) {
        SetWindowTextW(modbusProgressText_, text.c_str());
    }
}

void NativeMainWindow::runModbusScan() {
    if (serialIoState_.isOwnedBy(NativeSerialIoOwner::ModbusScan)) {
        requestCancelModbusScan();
        return;
    }
    if (!serialPort_.isOpen()) {
        setStatus(tx(T::ConnectBeforeModbus));
        return;
    }
    if (!store_.isOpen()) {
        setStatus(tx(T::StorageModbusClosed));
        return;
    }
    if (!serialIoState_.allowsModbusScan()) {
        setStatus(serialIoBusyStatus());
        return;
    }

    NativeModbusScanRequestInput requestInput;
    requestInput.slaveId = textToInt(scanSlaveEdit_, 1);
    requestInput.functionCode = static_cast<::svm::core::Byte>(selectedComboData(scanFunctionCombo_, 3));
    requestInput.startAddress = textToInt(scanStartEdit_, 0);
    requestInput.endAddress = textToInt(scanEndEdit_, 15);
    requestInput.scanSessionId = "scan-" + nativeTimestampIdText();
    requestInput.startedAtUtc = nativeUtcTimestampText();

    auto requestResult = nativeBuildModbusScanRequest(requestInput);
    if (!requestResult.ok) {
        setStatus(utf8ToWide(requestResult.errorMessage));
        MessageBoxW(window_, utf8ToWide(requestResult.errorMessage).c_str(), tx(T::ModbusInvalidTitle), MB_ICONWARNING | MB_OK);
        return;
    }

    appendLog(uiString(T::SystemModbusStartPrefix) + utf8ToWide(requestInput.scanSessionId));
    closeModbusScanThread();
    modbusScanCancelRequested_ = false;
    disconnectAfterModbusScan_ = false;
    if (!serialIoState_.tryAcquire(NativeSerialIoOwner::ModbusScan)) {
        setStatus(serialIoBusyStatus());
        return;
    }
    setModbusScanRunningUi(true);
    updateModbusScanProgress(0, requestResult.request.plan.blocks.size(), 0, 0, 0);
    setStatus(tx(T::ModbusRunning));

    auto context = std::make_unique<NativeModbusScanContext>();
    context->notifyWindow = window_;
    context->serialPort = &serialPort_;
    context->cancelRequested = &modbusScanCancelRequested_;
    context->plan = std::move(requestResult.request.plan);
    context->execution = std::move(requestResult.request.execution);
    context->scanSessionId = requestInput.scanSessionId;
    context->timeoutErrorMessage = wideToUtf8(tx(T::ModbusTimeout));
    context->threadExceptionMessage = wideToUtf8(L"Modbus \u626B\u63CF\u7EBF\u7A0B\u5F02\u5E38\u7EC8\u6B62\u3002");
    modbusScanThread_ = CreateThread(nullptr, 0, &nativeModbusScanThreadProc, context.get(), 0, nullptr);
    if (modbusScanThread_ == nullptr) {
        setModbusScanRunningUi(false);
        setStatus(tx(T::ModbusThreadCreateFailed));
        return;
    }
    context.release();
}

void NativeMainWindow::requestCancelModbusScan() {
    if (!modbusScanRunning_) {
        return;
    }
    modbusScanCancelRequested_ = true;
    setStatus(tx(T::ModbusCancelRequestedStatus));
}

void NativeMainWindow::handleModbusScanProgress(NativeModbusScanProgress* progressPointer) {
    std::unique_ptr<NativeModbusScanProgress> progress(progressPointer);
    if (!progress) {
        return;
    }

    updateModbusScanProgress(
        progress->completedBlocks,
        progress->totalBlocks,
        progress->successBlocks,
        progress->failedBlocks,
        progress->observations);
    if (modbusScanRunning_) {
        setStatus(uiString(T::ModbusProgressPrefix)
            + std::to_wstring(progress->completedBlocks)
            + L"/"
            + std::to_wstring(progress->totalBlocks)
            + tx(T::ModbusProgressBlocks)
            + tx(T::ModbusProgressSuccessBlocks)
            + std::to_wstring(progress->successBlocks)
            + tx(T::ModbusProgressFailedBlocks)
            + std::to_wstring(progress->failedBlocks)
            + tx(T::ModbusProgressObservationsShort)
            + std::to_wstring(progress->observations)
            + tx(T::ChinesePeriod));
    }
}

void NativeMainWindow::handleModbusScanDataBatch(NativeModbusScanDataBatch* batchPointer) {
    std::unique_ptr<NativeModbusScanDataBatch> batch(batchPointer);
    if (!batch) {
        return;
    }

    for (const native_storage::RawIoEvent& event : batch->rawEvents) {
        if (event.direction == "Tx") {
            statusCountersState_.addTxBytes(static_cast<std::uint64_t>(event.payload.size()));
        } else if (event.direction == "Rx") {
            statusCountersState_.addRxBytes(static_cast<std::uint64_t>(event.payload.size()));
        }
    }
    if (!batch->rawEvents.empty()) {
        updateStatusSegments();
        saveRawEvents(std::move(batch->rawEvents));
    }
    for (NativeLogEntry& entry : batch->logEntries) {
        addLogEntry(std::move(entry));
    }
}

void NativeMainWindow::handleModbusScanDone(NativeModbusScanResult* resultPointer) {
    std::unique_ptr<NativeModbusScanResult> result(resultPointer);
    const bool shouldDisconnectAfterScan = disconnectAfterModbusScan_;
    disconnectAfterModbusScan_ = false;
    closeModbusScanThread();
    if (!result) {
        setModbusScanRunningUi(false);
        return;
    }

    updateCompletedModbusScanProgress(*result);
    const std::wstring summary = persistCompletedModbusScan(*result);
    if (!summary.empty()) {
        appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    }
    setModbusScanRunningUi(false);
    if (handleCompletedModbusScanDisconnect(*result, shouldDisconnectAfterScan)) {
        return;
    }
    if (result->serialFailed && !result->errorMessage.empty()) {
        handleSerialFailure(result->errorMessage);
        return;
    }
    setStatus(summary);
}

void NativeMainWindow::closeModbusScanThread() {
    if (modbusScanThread_ == nullptr) {
        return;
    }
    WaitForSingleObject(modbusScanThread_, INFINITE);
    CloseHandle(modbusScanThread_);
    modbusScanThread_ = nullptr;
}

void NativeMainWindow::updateCompletedModbusScanProgress(const NativeModbusScanResult& result) {
    updateModbusScanProgress(
        result.execution.attempts.size(),
        static_cast<std::size_t>(std::max(0, result.execution.session.requestCount)),
        static_cast<std::size_t>(std::max(0, result.execution.session.successBlockCount)),
        static_cast<std::size_t>(std::max(0, result.execution.session.failedBlockCount)),
        result.execution.observations.size());
}

std::wstring NativeMainWindow::persistCompletedModbusScan(const NativeModbusScanResult& result) {
    if (!store_.saveScanExecution(result.execution)) {
        return utf8ToWide(store_.lastErrorText());
    }
    if (result.cancelled) {
        return tx(T::ModbusCancelledStatus);
    }
    return uiString(T::ModbusSummaryPrefix)
        + std::to_wstring(result.execution.session.successBlockCount)
        + uiString(T::ModbusFailedBlocks)
        + std::to_wstring(result.execution.session.failedBlockCount)
        + uiString(T::ModbusObservations)
        + std::to_wstring(result.execution.observations.size())
        + uiString(T::ChinesePeriod);
}

bool NativeMainWindow::handleCompletedModbusScanDisconnect(const NativeModbusScanResult& result, bool shouldDisconnectAfterScan) {
    if (!shouldDisconnectAfterScan) {
        return false;
    }
    if (result.serialFailed && !result.errorMessage.empty()) {
        appendLog(NativeLogKind::Error, uiString(T::SystemSerialFailedPrefix) + utf8ToWide(result.errorMessage));
    }
    closeSerialPort(tx(T::ModbusDisconnectedAfterCancelStatus));
    return true;
}

void NativeMainWindow::setModbusScanRunningUi(bool running) {
    modbusScanRunning_ = running;
    if (running && !serialIoState_.isOwnedBy(NativeSerialIoOwner::ModbusScan)) {
        serialIoState_.tryAcquire(NativeSerialIoOwner::ModbusScan);
    } else if (!running) {
        serialIoState_.release(NativeSerialIoOwner::ModbusScan);
    }
    if (running) {
        KillTimer(window_, IDT_SERIAL_POLL);
        KillTimer(window_, IDT_TIMED_SEND);
    } else if (serialPort_.isOpen()) {
        SetTimer(window_, IDT_SERIAL_POLL, 50, nullptr);
        updateTimedSendTimer();
    }
    const NativeModbusScanUiSnapshot ui = modbusScanUiState_.snapshot(running);
    setControlText(modbusButton_, ui.buttonMode == NativeModbusScanButtonMode::Stop ? tx(T::ModbusStopButton) : tx(T::ModbusScanButton));

    for (HWND control : {
             portCombo_,
             refreshButton_,
             saveProfileButton_,
             baudCombo_,
             dataBitsCombo_,
             parityCombo_,
             stopBitsCombo_,
             flowControlCombo_,
             dtrCheck_,
             rtsCheck_,
             autoReconnectCheck_,
             scanSlaveEdit_,
             scanFunctionCombo_,
             scanStartEdit_,
             scanEndEdit_,
             analysisButton_,
             ruleVerifyButton_,
             exportReportButton_,
             sendEdit_,
             sendButton_,
             timedSendCheck_,
             timedPeriodEdit_,
             fileSendButton_,
             fileBrowseButton_,
             filePathEdit_,
         }) {
        enableControl(control, ui.exclusiveControlsEnabled);
    }
    for (HWND button : quickSendButtons_) {
        enableControl(button, ui.exclusiveControlsEnabled);
    }
    updateRtsControlState();
}

} // namespace svm::win32

#endif
