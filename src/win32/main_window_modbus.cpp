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

constexpr DWORD kModbusThreadJoinBudgetMs =
    static_cast<DWORD>(svm::transport::kSerialTerminalResultTargetMs);

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
    const svm::transport::SerialSessionSnapshot initialSession = serialLifecycle_.snapshot();
    const NativeModbusScanDecision scanDecision = modbusAnalysisController_.scanDecision(
        serialIoState_.isOwnedBy(NativeSerialIoOwner::ModbusScan),
        initialSession.open(),
        store_.isOpen(),
        serialIoState_);
    if (scanDecision.cancelsScan()) {
        requestCancelModbusScan();
        return;
    }
    if (scanDecision.action == NativeModbusAction::ConnectRequired) {
        setStatus(tx(T::ConnectBeforeModbus));
        return;
    }
    if (scanDecision.action == NativeModbusAction::StorageUnavailable) {
        setStatus(tx(T::StorageModbusClosed));
        return;
    }
    if (scanDecision.action == NativeModbusAction::SerialBusy) {
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

    core::DangerousOperationRequest dangerousRequest;
    dangerousRequest.kind = core::DangerousOperationKind::ManualSerialWrite;
    dangerousRequest.slaveId = requestInput.slaveId;
    dangerousRequest.functionCode = requestInput.functionCode;
    dangerousRequest.address = requestInput.startAddress;
    dangerousRequest.quantity = requestInput.endAddress - requestInput.startAddress + 1;
    if (!confirmDangerousOperation(dangerousRequest, L"Modbus 写操作会在创建执行线程前要求确认；当前只读扫描不会弹出确认。")) {
        return;
    }

    if (closeModbusScanThread() != NativeModbusThreadCloseResult::Settled) {
        return;
    }
    delete modbusScanTerminalResult_.exchange(nullptr, std::memory_order_acq_rel);
    const svm::transport::SerialSessionSnapshot scanSession = serialLifecycle_.snapshot();
    if (!scanSession.open()
        || scanSession.generation == svm::transport::kUnassignedSerialSessionGeneration) {
        setStatus(tx(T::ConnectBeforeModbus));
        return;
    }
    if (!store_.isOpen()) {
        setStatus(tx(T::StorageModbusClosed));
        return;
    }
    modbusScanCancelRequested_ = false;
    disconnectAfterModbusScan_ = false;
    if (!serialIoState_.tryAcquire(scanDecision.owner)) {
        setStatus(serialIoBusyStatus());
        return;
    }
    modbusScanGeneration_ = scanSession.generation;
    appendLog(uiString(T::SystemModbusStartPrefix) + utf8ToWide(requestInput.scanSessionId));
    setModbusScanRunningUi(true);
    updateModbusScanProgress(0, requestResult.request.plan.blocks.size(), 0, 0, 0);
    setStatus(tx(T::ModbusRunning));

    auto context = std::make_unique<NativeModbusScanContext>();
    context->notifyWindow = window_;
    context->byteStream = &serialByteStream_;
    context->generation = scanSession.generation;
    context->endpoint = scanSession.endpoint;
    context->generationIsCurrent = [session = &serialLifecycle_](svm::transport::SerialSessionGeneration generation) {
        const svm::transport::SerialSessionSnapshot current = session->snapshot();
        return current.open() && current.generation == generation;
    };
    context->cancelRequested = &modbusScanCancelRequested_;
    context->terminalResult = &modbusScanTerminalResult_;
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
    if (modbusScanThread_ != nullptr && !CancelSynchronousIo(modbusScanThread_)) {
        const DWORD nativeCode = GetLastError();
        if (nativeCode != ERROR_NOT_FOUND) {
            appendLog(
                NativeLogKind::Error,
                L"中断 Modbus 串口操作失败 (native="
                    + std::to_wstring(nativeCode)
                    + L")");
        }
    }
    setStatus(tx(T::ModbusCancelRequestedStatus));
}

void NativeMainWindow::handleModbusScanProgress(NativeModbusScanProgress* progressPointer) {
    std::unique_ptr<NativeModbusScanProgress> progress(progressPointer);
    if (!progress
        || !modbusScanRunning_.load(std::memory_order_relaxed)
        || !nativeModbusScanMessageMatchesSession(
            progress->generation,
            modbusScanGeneration_,
            serialLifecycle_.snapshot(),
            true)) {
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
    if (!batch
        || !modbusScanRunning_.load(std::memory_order_relaxed)
        || !nativeModbusScanMessageMatchesSession(
            batch->generation,
            modbusScanGeneration_,
            serialLifecycle_.snapshot(),
            true)) {
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

void NativeMainWindow::handleModbusScanDone() {
    std::unique_ptr<NativeModbusScanResult> result(
        modbusScanTerminalResult_.exchange(nullptr, std::memory_order_acq_rel));
    if (!result || result->generation != modbusScanGeneration_) {
        return;
    }
    const NativeModbusThreadCloseResult closeResult = closeModbusScanThread();
    if (closeResult == NativeModbusThreadCloseResult::NotJoined) {
        modbusScanTerminalResult_.store(result.release(), std::memory_order_release);
        return;
    }
    drainPendingModbusScanDataMessages();
    const bool shouldDisconnectAfterScan = disconnectAfterModbusScan_;
    disconnectAfterModbusScan_ = false;
    const bool resultMatchesSession = result
        && nativeModbusScanMessageMatchesSession(
            result->generation,
            modbusScanGeneration_,
            serialLifecycle_.snapshot(),
            result->serialFailed);
    setModbusScanRunningUi(false);
    if (!resultMatchesSession) {
        return;
    }

    updateCompletedModbusScanProgress(*result);
    const std::wstring summary = persistCompletedModbusScan(*result);
    if (!summary.empty()) {
        appendLog(std::wstring(L"[\u7CFB\u7EDF] ") + summary);
    }
    if (handleCompletedModbusScanDisconnect(shouldDisconnectAfterScan)) {
        return;
    }
    if (result->serialFailed && !result->errorMessage.empty()) {
        handleSerialFailure(result->errorMessage, false);
        return;
    }
    setStatus(summary);
}

NativeMainWindow::NativeModbusThreadCloseResult NativeMainWindow::closeModbusScanThread() {
    if (modbusScanThread_ == nullptr) {
        return NativeModbusThreadCloseResult::Settled;
    }
    const DWORD waitResult = WaitForSingleObject(modbusScanThread_, kModbusThreadJoinBudgetMs);
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD nativeCode = waitResult == WAIT_FAILED
            ? GetLastError()
            : waitResult == WAIT_TIMEOUT
                ? ERROR_TIMEOUT
                : waitResult;
        const std::wstring message = L"等待 Modbus 扫描线程结束失败 (native="
            + std::to_wstring(nativeCode) + L")";
        appendLog(NativeLogKind::Error, message);
        setStatus(message);
        return NativeModbusThreadCloseResult::NotJoined;
    }
    if (!CloseHandle(modbusScanThread_)) {
        const std::wstring message = L"关闭 Modbus 扫描线程句柄失败 (native="
            + std::to_wstring(GetLastError()) + L")";
        appendLog(NativeLogKind::Error, message);
        setStatus(message);
        return NativeModbusThreadCloseResult::HandleCloseFailed;
    }
    modbusScanThread_ = nullptr;
    return NativeModbusThreadCloseResult::Settled;
}

void NativeMainWindow::drainPendingModbusScanDataMessages() {
    MSG message = {};
    while (PeekMessageW(&message, window_, kNativeModbusScanDataMessage, kNativeModbusScanDataMessage, PM_REMOVE)) {
        handleModbusScanDataBatch(reinterpret_cast<NativeModbusScanDataBatch*>(message.lParam));
    }
}

void NativeMainWindow::settlePendingModbusScanMessages() {
    MSG message = {};
    while (PeekMessageW(&message, window_, kNativeModbusScanProgressMessage, kNativeModbusScanProgressMessage, PM_REMOVE)) {
        delete reinterpret_cast<NativeModbusScanProgress*>(message.lParam);
    }
    while (PeekMessageW(&message, window_, kNativeModbusScanDataMessage, kNativeModbusScanDataMessage, PM_REMOVE)) {
        std::unique_ptr<NativeModbusScanDataBatch> batch(
            reinterpret_cast<NativeModbusScanDataBatch*>(message.lParam));
        if (batch && !batch->rawEvents.empty()) {
            saveRawEvents(std::move(batch->rawEvents));
        }
    }
    while (PeekMessageW(&message, window_, kNativeModbusScanDoneMessage, kNativeModbusScanDoneMessage, PM_REMOVE)) {
    }
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
        reportStorageFailure(L"保存 Modbus 扫描结果");
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

bool NativeMainWindow::handleCompletedModbusScanDisconnect(bool shouldDisconnectAfterScan) {
    if (!shouldDisconnectAfterScan) {
        return false;
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
        modbusScanGeneration_ = svm::transport::kUnassignedSerialSessionGeneration;
    }
    if (running) {
        KillTimer(window_, IDT_SERIAL_POLL);
        KillTimer(window_, IDT_TIMED_SEND);
    } else if (serialLifecycle_.snapshot().open()) {
        SetTimer(window_, IDT_SERIAL_POLL, 50, nullptr);
        updateTimedSendTimer();
    }
    const NativeModbusScanUiSnapshot ui = modbusAnalysisController_.scanUiSnapshot(modbusScanUiState_, running);
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
