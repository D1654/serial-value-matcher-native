#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_control_utils.h"
#include "win32/native_file_send_state.h"
#include "win32/native_send_codec.h"
#include "win32/native_send_history_state.h"
#include "win32/native_time_utils.h"
#include "win32/native_ui_preferences.h"
#include "win32/resource.h"
#include "win32/ui_text.h"
#include "win32/utf8_win32.h"

#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <commdlg.h>
#include <filesystem>
#include <sstream>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

std::string auditPayloadText(const svm::core::DangerousOperationAuditEvent& event) {
    std::ostringstream output;
    output << "dangerous_operation_confirmation"
           << "\nstate=" << svm::core::dangerousOperationConfirmationStateName(event.state)
           << "\nsummary=" << event.summary;
    for (const auto& [key, value] : event.metadata) {
        output << "\n" << key << "=" << value;
    }
    const std::string text = output.str();
    return text;
}

std::vector<std::uint8_t> auditPayloadBytes(const svm::core::DangerousOperationAuditEvent& event) {
    const std::string text = auditPayloadText(event);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::wstring dangerousOperationTitle() {
    return L"确认危险操作";
}

svm::transport::SerialDeadline serialWriteDeadline(
    const svm::transport::SerialSessionSnapshot& session) {
    return {
        .expiresAt = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(std::max(1, session.options.writeTimeoutMs)),
    };
}

std::wstring dangerousOperationPrompt(
    const svm::core::DangerousOperationPolicyResult& policy,
    const std::wstring& promptDetail) {
    std::wstring prompt = L"此操作可能持续写入或改变外部设备状态。\r\n\r\n";
    prompt += L"操作摘要：";
    prompt += utf8ToWide(policy.summary);
    if (!policy.reason.empty()) {
        prompt += L"\r\n原因：";
        prompt += utf8ToWide(policy.reason);
    }
    if (!promptDetail.empty()) {
        prompt += L"\r\n";
        prompt += promptDetail;
    }
    prompt += L"\r\n\r\n选择“是”才会继续执行；选择“否”、关闭窗口或提示失败都会取消。";
    return prompt;
}

} // namespace

bool NativeMainWindow::confirmDangerousOperation(const core::DangerousOperationRequest& request, const std::wstring& promptDetail) {
    const core::DangerousOperationPolicyResult policy = core::evaluateDangerousOperation(request);
    if (!policy.requiresConfirmation) {
        return true;
    }

    const int dialogResult = MessageBoxW(
        window_,
        dangerousOperationPrompt(policy, promptDetail).c_str(),
        dangerousOperationTitle().c_str(),
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2 | MB_APPLMODAL);
    const core::DangerousOperationConfirmationState state = core::dangerousOperationConfirmationStateFromDialogResult(dialogResult);
    recordDangerousOperationAudit(policy, request.kind, state);
    if (core::dangerousOperationConfirmationSatisfied(state)) {
        return true;
    }

    setStatus(state == core::DangerousOperationConfirmationState::PromptFailed
        ? L"危险操作确认提示失败，已取消执行。"
        : L"危险操作已取消，未执行。");
    return false;
}

void NativeMainWindow::recordDangerousOperationAudit(
    const core::DangerousOperationPolicyResult& policy,
    core::DangerousOperationKind kind,
    core::DangerousOperationConfirmationState state) {
    const core::DangerousOperationAuditEvent audit = core::makeDangerousOperationAuditEvent(policy, kind, state);
    const std::wstring logLine = std::wstring(L"[审计] 危险操作确认：")
        + utf8ToWide(policy.summary)
        + L" -> "
        + utf8ToWide(core::dangerousOperationConfirmationStateName(state));
    appendLog(NativeLogKind::System, logLine);

    native_storage::RawIoEvent event;
    event.sessionId = sessionId_;
    event.direction = "Audit";
    event.timestampUtc = nativeUtcTimestampText();
    event.endpoint = "local://dangerous-operation";
    event.payload = auditPayloadBytes(audit);
    if (store_.isOpen()) {
        store_.appendRawEvent(event);
    }
}

void NativeMainWindow::setSendModeStatus() {
    const int mode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    switch (mode) {
    case 1:
        setStatus(tx(T::SendModeHexStatus));
        break;
    case 2:
        setStatus(tx(T::SendModeDecimalStatus));
        break;
    case 3:
        setStatus(tx(T::SendModeBinaryStatus));
        break;
    default:
        setStatus(tx(T::SendModeTextStatus));
        break;
    }
}

std::vector<std::uint8_t> NativeMainWindow::payloadFromInput(std::wstring* errorText) const {
    return payloadFromText(controlText(sendEdit_), errorText);
}

std::vector<std::uint8_t> NativeMainWindow::payloadFromText(const std::wstring& text, std::wstring* errorText) const {
    if (errorText != nullptr) {
        errorText->clear();
    }
    NativeSendCodecErrors errors;
    errors.hexInvalidChar = tx(T::HexInvalidChar);
    errors.hexOddNibble = tx(T::HexOddNibble);
    errors.invalidDecimal = tx(T::SendModeInvalidDecimal);
    errors.invalidBinary = tx(T::SendModeInvalidBinary);
    errors.textEncodingFailed = tx(T::TextEncodingFailed);

    NativeSendPayloadOptions options;
    options.mode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    options.textCodePage = selectedTextCodePage();
    options.lineEnding = static_cast<int>(selectedComboData(lineEndingCombo_, 0));

    const NativeSendPayloadResult result = nativeBuildSendPayload(text, options, errors);
    if (errorText != nullptr) {
        *errorText = result.errorText;
    }
    return result.payload;
}

unsigned int NativeMainWindow::selectedTextCodePage() const {
    return static_cast<unsigned int>(selectedComboData(textEncodingCombo_, CP_UTF8));
}

void NativeMainWindow::refreshSendHistory() {
    SendMessageW(historyCombo_, CB_RESETCONTENT, 0, 0);
    sendHistoryState_.clear();
    SendMessageW(historyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tx(T::SendHistory)));
    if (store_.isOpen()) {
        sendHistoryState_.setEntries(store_.recentSendHistory(30));
        for (std::size_t index = 0; index < sendHistoryState_.entries().size(); ++index) {
            const native_storage::SendHistoryEntry& item = sendHistoryState_.entries()[index];
            const LRESULT comboIndex = SendMessageW(historyCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(utf8ToWide(item.content).c_str()));
            if (comboIndex >= 0) {
                SendMessageW(historyCombo_, CB_SETITEMDATA, static_cast<WPARAM>(comboIndex), static_cast<LPARAM>(sendHistoryState_.itemDataForIndex(index)));
            }
        }
    }
    SendMessageW(historyCombo_, CB_SETCURSEL, 0, 0);
}

void NativeMainWindow::applySelectedHistory() {
    const LRESULT index = SendMessageW(historyCombo_, CB_GETCURSEL, 0, 0);
    if (index <= 0) {
        return;
    }
    const LRESULT itemData = SendMessageW(historyCombo_, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    if (itemData <= 0) {
        setControlText(sendEdit_, controlText(historyCombo_));
        return;
    }
    const std::optional<native_storage::SendHistoryEntry> history = sendHistoryState_.entryFromItemData(static_cast<NativeSendHistoryItemData>(itemData));
    if (!history.has_value()) {
        return;
    }

    setControlText(sendEdit_, utf8ToWide(history->content));
    selectComboData(sendModeCombo_, history->payloadMode);
    selectComboData(lineEndingCombo_, history->lineEnding);
    selectComboData(textEncodingCombo_, history->textEncodingCodePage);
    setStatus(tx(T::SendHistoryRestored));
}

void NativeMainWindow::sendPayload() {
    sendPayloadFromText(controlText(sendEdit_), true);
}

bool NativeMainWindow::sendPayloadFromText(const std::wstring& text, bool saveHistory) {
    const NativeSerialSendDecision availability = serialSendController_.manualSendAvailability(
        serialLifecycle_.snapshot().open(),
        serialIoState_);
    if (availability.kind == NativeSerialSendDecisionKind::SerialNotConnected) {
        setStatus(tx(T::SerialNotConnectedSend));
        return false;
    }
    if (availability.kind == NativeSerialSendDecisionKind::SerialIoBusy) {
        setStatus(serialIoBusyStatus());
        return false;
    }

    std::wstring errorText;
    const std::vector<std::uint8_t> payload = payloadFromText(text, &errorText);
    const NativeSerialSendDecision payloadDecision = serialSendController_.manualPayloadDecision(!errorText.empty(), payload.empty());
    if (payloadDecision.kind == NativeSerialSendDecisionKind::PayloadInvalid) {
        setStatus(errorText);
        return false;
    }
    if (payloadDecision.kind == NativeSerialSendDecisionKind::PayloadEmpty) {
        setStatus(tx(T::EmptyPayload));
        return false;
    }

    if (sendControlState_.timedSendEnabled() && !timedSendConfirmed_) {
        core::DangerousOperationRequest request;
        request.kind = core::DangerousOperationKind::TimedSerialWrite;
        request.payloadBytes = payload.size();
        request.repeatCount = 2;
        if (!confirmDangerousOperation(request, L"定时发送会按当前周期重复写入串口。")) {
            SendMessageW(timedSendCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
            sendControlState_.setTimedSendEnabled(false);
            updateTimedSendTimer();
            saveUiPreferences();
            return false;
        }
        timedSendConfirmed_ = true;
    } else if (!sendControlState_.timedSendEnabled()) {
        timedSendConfirmed_ = false;
    }

    const NativeSerialSendDecision acquireDecision = serialSendController_.manualAcquireDecision(serialIoState_.tryAcquire(availability.owner));
    if (!acquireDecision.allowed()) {
        setStatus(serialIoBusyStatus());
        return false;
    }
    const bool queued = enqueueManualSerialWrite(payload, text, saveHistory);
    serialIoState_.release(NativeSerialIoOwner::ManualSend);
    return queued;
}

bool NativeMainWindow::enqueueManualSerialWrite(std::vector<std::uint8_t> payload, const std::wstring& text, bool saveHistory) {
    const svm::transport::SerialSessionSnapshot session = serialLifecycle_.snapshot();
    NativePendingSerialWrite pending;
    pending.source = NativePendingSerialWriteSource::Manual;
    pending.payload = payload;
    pending.manualText = text;
    pending.saveHistory = saveHistory;
    pending.payloadMode = static_cast<int>(selectedComboData(sendModeCombo_, 0));
    pending.lineEnding = static_cast<int>(selectedComboData(lineEndingCombo_, 0));
    pending.textCodePage = static_cast<int>(selectedTextCodePage());

    const svm::transport::SerialWriteAdmissionResult result = serialWriteScheduler_.enqueueWrite(
        std::move(payload),
        serialWriteDeadline(session));
    if (!result.accepted()) {
        updateSerialWriteQueueStatus();
        const std::wstring message = serialOperationErrorMessage(result, "加入串口写入队列");
        setStatus(message);
        if (result.status == svm::transport::SerialOperationStatus::Failed
            || result.status == svm::transport::SerialOperationStatus::Timeout
            || result.status == svm::transport::SerialOperationStatus::Disconnected) {
            handleSerialFailure(wideToUtf8(message));
        }
        return false;
    }
    const NativeSerialWriteKey key = nativeSerialWriteKeyForAdmission(result, session.generation);
    if (!key.assigned()) {
        updateSerialWriteQueueStatus();
        const std::wstring message = L"串口写入请求未返回有效的会话身份。";
        setStatus(message);
        handleSerialFailure(wideToUtf8(message));
        return false;
    }

    pending.key = key;
    pendingSerialWrites_.push_back(std::move(pending));
    updateSerialWriteQueueStatus();
    setStatus(L"已加入发送队列 " + std::to_wstring(result.byteCount) + uiString(T::BytesSuffix));
    return true;
}

void NativeMainWindow::sendQuickPayload(std::size_t index) {
    const std::wstring text = sendControlState_.isQuickSendIndexValid(index, quickSendEdits_.size())
        ? controlText(quickSendEdits_[index])
        : std::wstring();
    const NativeSerialSendDecision decision = serialSendController_.quickSendDecision(
        sendControlState_,
        index,
        quickSendEdits_.size(),
        text);
    if (decision.ignored()) {
        return;
    }
    if (decision.kind == NativeSerialSendDecisionKind::QuickSendEmpty) {
        setStatus(tx(T::QuickSendEmptyStatus));
        return;
    }
    sendPayloadFromText(text, false);
}

void NativeMainWindow::updateTimedSendTimer() {
    KillTimer(window_, IDT_TIMED_SEND);
    const NativeTimedSendTimerDecision decision = serialSendController_.timedSendDecision(
        sendControlState_,
        serialLifecycle_.snapshot().open(),
        serialIoState_,
        textToInt(timedPeriodEdit_, kNativeDefaultTimedSendPeriodMs));
    if (!decision.shouldRun) {
        return;
    }
    SetTimer(window_, IDT_TIMED_SEND, static_cast<UINT>(decision.periodMs), nullptr);
}

void NativeMainWindow::browseFileSend() {
    wchar_t fileName[MAX_PATH] = {};
    const std::wstring current = controlText(filePathEdit_);
    if (!current.empty()) {
        wcsncpy_s(fileName, current.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = tx(T::FileSendFilter);
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = tx(T::FileSendDialogTitle);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    setControlText(filePathEdit_, fileName);
    fileSend_.setPath(std::filesystem::path(fileName));
    saveUiPreferences();
}

void NativeMainWindow::startFileSend() {
    const std::wstring pathText = controlText(filePathEdit_);
    const svm::transport::SerialSessionSnapshot requestedSession = serialLifecycle_.snapshot();
    const NativeSerialSendDecision startDecision = serialSendController_.fileStartDecision(
        requestedSession.open(),
        serialIoState_,
        fileSend_.active(),
        pathText);
    if (startDecision.kind == NativeSerialSendDecisionKind::SerialNotConnected) {
        setStatus(tx(T::SerialNotConnectedSend));
        return;
    }
    if (startDecision.kind == NativeSerialSendDecisionKind::SerialIoBusy) {
        setStatus(serialIoBusyStatus());
        return;
    }
    if (serialIoState_.hasPendingSerialWrites()) {
        setStatus(L"串口写入队列仍有待发送数据，请稍后再开始文件发送。");
        return;
    }
    const bool unsettledFileWrite = std::any_of(
        pendingSerialWrites_.begin(),
        pendingSerialWrites_.end(),
        [](const NativePendingSerialWrite& pending) {
            return pending.source == NativePendingSerialWriteSource::File;
        });
    if (unsettledFileWrite) {
        setStatus(L"上一次文件发送仍在结算，请稍后再试。");
        return;
    }
    if (startDecision.ignored()) {
        return;
    }
    if (startDecision.kind == NativeSerialSendDecisionKind::FilePathEmpty) {
        setStatus(tx(T::FileSendNoFile));
        return;
    }

    core::DangerousOperationRequest request;
    request.kind = core::DangerousOperationKind::FileSerialSend;
    request.payloadBytes = 0;
    if (!confirmDangerousOperation(request, L"文件发送会按分块方式连续写入串口。")) {
        return;
    }

    const NativeFileSendOpenResult openResult = fileSend_.open(std::filesystem::path(pathText));
    if (!openResult.ok()) {
        setStatus(uiString(T::FileSendOpenFailedPrefix) + pathText);
        return;
    }
    const NativeSerialSendDecision acquireDecision = serialSendController_.fileAcquireDecision(serialIoState_.tryAcquire(startDecision.owner));
    if (!acquireDecision.allowed()) {
        fileSend_.close();
        setStatus(serialIoBusyStatus());
        return;
    }

    const svm::transport::SerialSessionSnapshot activeSession = serialLifecycle_.snapshot();
    if (!activeSession.open() || activeSession.generation != requestedSession.generation) {
        fileSend_.close();
        serialIoState_.release(NativeSerialIoOwner::FileSend);
        setStatus(tx(T::DisconnectedStatus));
        return;
    }
    fileSendGeneration_ = activeSession.generation;

    KillTimer(window_, IDT_TIMED_SEND);
    timedSendConfirmed_ = false;
    updateFileSendProgress();
    enableControl(fileSendButton_, false);
    enableControl(fileBrowseButton_, false);
    enableControl(filePathEdit_, false);
    enableControl(fileStopButton_, true);
    const int delayMs = std::max<int>(1, static_cast<int>(selectedComboData(fileDelayCombo_, 0)));
    SetTimer(window_, IDT_FILE_SEND, static_cast<UINT>(delayMs), nullptr);
    setStatus(uiString(T::FileSendStartedPrefix) + pathText);
}

void NativeMainWindow::stopFileSend(const std::wstring& statusText) {
    KillTimer(window_, IDT_FILE_SEND);
    const bool wasActive = fileSend_.active();
    const svm::transport::SerialSessionGeneration fileGeneration = fileSendGeneration_;
    fileSendGeneration_ = svm::transport::kUnassignedSerialSessionGeneration;
    fileSend_.close();
    if (wasActive) {
        const svm::transport::SerialSessionSnapshot session = serialLifecycle_.snapshot();
        if (fileGeneration != svm::transport::kUnassignedSerialSessionGeneration
            && session.generation == fileGeneration
            && serialIoState_.isOwnedBy(NativeSerialIoOwner::FileSend)) {
            const std::vector<svm::transport::SerialTerminalResult> cancelled =
                serialWriteScheduler_.cancelPendingWrites();
            for (const auto& result : cancelled) {
                const auto found = std::find_if(
                    pendingSerialWrites_.begin(),
                    pendingSerialWrites_.end(),
                    [&result, fileGeneration](const NativePendingSerialWrite& pending) {
                        return pending.source == NativePendingSerialWriteSource::File
                            && nativeSerialWriteCancellationMatches(
                                pending.key,
                                result,
                                fileGeneration);
                    });
                if (found != pendingSerialWrites_.end()) {
                    pendingSerialWrites_.erase(found);
                }
            }
        }
        updateSerialWriteQueueStatus();
        serialIoState_.release(NativeSerialIoOwner::FileSend);
    }
    enableControl(fileSendButton_, true);
    enableControl(fileBrowseButton_, true);
    enableControl(filePathEdit_, true);
    enableControl(fileStopButton_, false);
    updateFileSendProgress();
    if (!statusText.empty()) {
        setStatus(statusText);
    } else if (wasActive) {
        setStatus(tx(T::FileSendStoppedStatus));
    }
    if (wasActive) {
        updateTimedSendTimer();
    }
}

void NativeMainWindow::pumpFileSend() {
    drainSerialWriteResults();
    const svm::transport::SerialSessionSnapshot session = serialLifecycle_.snapshot();
    const NativeSerialSendDecision pumpDecision = serialSendController_.filePumpDecision(
        fileSend_.active(),
        session.open() && session.generation == fileSendGeneration_,
        serialIoState_);
    if (pumpDecision.ignored()) {
        return;
    }
    if (pumpDecision.kind == NativeSerialSendDecisionKind::FileDisconnected) {
        stopFileSend(tx(T::DisconnectedStatus));
        return;
    }
    if (pumpDecision.kind == NativeSerialSendDecisionKind::SerialIoBusy) {
        stopFileSend(serialIoBusyStatus());
        return;
    }
    if (serialIoState_.hasPendingSerialWrites()) {
        return;
    }

    NativeFileSendChunk chunk = fileSend_.readNextChunk(kNativeFileSendChunkBytes);
    if (chunk.status == NativeFileSendReadStatus::End) {
        const std::wstring done = uiString(T::FileSendDonePrefix) + std::to_wstring(fileSend_.sentBytes()) + uiString(T::BytesSuffix);
        stopFileSend(done);
        return;
    }
    if (!chunk.ready()) {
        stopFileSend(uiString(T::FileSendReadFailedPrefix) + controlText(filePathEdit_));
        return;
    }

    if (!enqueueFileSerialWrite(std::move(chunk.bytes))) {
        return;
    }
}

bool NativeMainWindow::enqueueFileSerialWrite(std::vector<std::uint8_t> payload) {
    const svm::transport::SerialSessionSnapshot session = serialLifecycle_.snapshot();
    if (!session.open() || session.generation != fileSendGeneration_) {
        stopFileSend(tx(T::DisconnectedStatus));
        return false;
    }
    NativePendingSerialWrite pending;
    pending.source = NativePendingSerialWriteSource::File;
    pending.payload = payload;

    const svm::transport::SerialWriteAdmissionResult result = serialWriteScheduler_.enqueueWrite(
        std::move(payload),
        serialWriteDeadline(session));
    if (!result.accepted()) {
        updateSerialWriteQueueStatus();
        const std::wstring message = serialOperationErrorMessage(result, "加入文件发送队列");
        stopFileSend(message);
        if (result.status == svm::transport::SerialOperationStatus::Failed
            || result.status == svm::transport::SerialOperationStatus::Timeout
            || result.status == svm::transport::SerialOperationStatus::Disconnected) {
            handleSerialFailure(wideToUtf8(message));
        }
        return false;
    }
    const NativeSerialWriteKey key = nativeSerialWriteKeyForAdmission(
        result,
        fileSendGeneration_);
    if (!key.assigned()) {
        updateSerialWriteQueueStatus();
        const std::wstring message = L"文件发送请求未返回有效的会话身份。";
        stopFileSend(message);
        handleSerialFailure(wideToUtf8(message));
        return false;
    }

    pending.key = key;
    pendingSerialWrites_.push_back(std::move(pending));
    updateSerialWriteQueueStatus();
    return true;
}

void NativeMainWindow::updateFileSendProgress() {
    if (fileProgress_ == nullptr) {
        return;
    }
    SendMessageW(fileProgress_, PBM_SETRANGE32, 0, 1000);
    SendMessageW(fileProgress_, PBM_SETPOS, static_cast<WPARAM>(fileSend_.progressPermille()), 0);
}

} // namespace svm::win32

#endif
