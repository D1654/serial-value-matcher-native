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
#include <commctrl.h>
#include <commdlg.h>
#include <filesystem>

namespace svm::win32 {
namespace {

using T = TextId;

const wchar_t* tx(T id) {
    return uiText(id);
}

} // namespace

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
    if (!serialPort_.isOpen()) {
        setStatus(tx(T::SerialNotConnectedSend));
        return false;
    }
    if (!serialIoState_.allowsManualSend()) {
        setStatus(serialIoBusyStatus());
        return false;
    }

    std::wstring errorText;
    const std::vector<std::uint8_t> payload = payloadFromText(text, &errorText);
    if (!errorText.empty()) {
        setStatus(errorText);
        return false;
    }
    if (payload.empty()) {
        setStatus(tx(T::EmptyPayload));
        return false;
    }

    if (!serialIoState_.tryAcquire(NativeSerialIoOwner::ManualSend)) {
        setStatus(serialIoBusyStatus());
        return false;
    }
    const SerialIoResult result = serialPort_.writeBytes(payload);
    serialIoState_.release(NativeSerialIoOwner::ManualSend);
    if (!result.ok) {
        setStatus(utf8ToWide(result.errorMessage));
        handleSerialFailure(result.errorMessage);
        return false;
    }

    saveRawEvent("Tx", payload);
    if (saveHistory && store_.isOpen()) {
        native_storage::SendHistoryEntry history = nativeMakeSendHistoryEntry(
            wideToUtf8(text),
            static_cast<int>(selectedComboData(sendModeCombo_, 0)),
            static_cast<int>(selectedComboData(lineEndingCombo_, 0)),
            static_cast<int>(selectedTextCodePage()),
            nativeUtcTimestampText());
        store_.saveSendHistory(history);
        refreshSendHistory();
    }
    appendPayloadLog(NativeLogKind::Tx, payload);
    statusCountersState_.addTxBytes(static_cast<std::uint64_t>(result.byteCount));
    updateStatusSegments();
    setStatus(uiString(T::SentPrefix) + std::to_wstring(result.byteCount) + uiString(T::BytesSuffix));
    return true;
}

void NativeMainWindow::sendQuickPayload(std::size_t index) {
    if (!sendControlState_.isQuickSendIndexValid(index, quickSendEdits_.size())) {
        return;
    }
    const std::wstring text = controlText(quickSendEdits_[index]);
    if (!sendControlState_.isQuickSendTextUsable(text)) {
        setStatus(tx(T::QuickSendEmptyStatus));
        return;
    }
    sendPayloadFromText(text, false);
}

void NativeMainWindow::updateTimedSendTimer() {
    KillTimer(window_, IDT_TIMED_SEND);
    const NativeTimedSendTimerDecision decision = sendControlState_.timerDecision(
        serialPort_.isOpen(),
        serialIoState_.allowsManualSend(),
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
    if (!serialPort_.isOpen()) {
        setStatus(tx(T::SerialNotConnectedSend));
        return;
    }
    if (!serialIoState_.allowsFileSend()) {
        setStatus(serialIoBusyStatus());
        return;
    }
    if (fileSend_.active()) {
        return;
    }

    const std::wstring pathText = controlText(filePathEdit_);
    if (pathText.empty()) {
        setStatus(tx(T::FileSendNoFile));
        return;
    }

    const NativeFileSendOpenResult openResult = fileSend_.open(std::filesystem::path(pathText));
    if (!openResult.ok()) {
        setStatus(uiString(T::FileSendOpenFailedPrefix) + pathText);
        return;
    }
    if (!serialIoState_.tryAcquire(NativeSerialIoOwner::FileSend)) {
        fileSend_.close();
        setStatus(serialIoBusyStatus());
        return;
    }

    KillTimer(window_, IDT_TIMED_SEND);
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
    fileSend_.close();
    if (wasActive) {
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
    if (!fileSend_.active()) {
        return;
    }
    if (!serialPort_.isOpen()) {
        stopFileSend(tx(T::DisconnectedStatus));
        return;
    }
    if (!serialIoState_.isOwnedBy(NativeSerialIoOwner::FileSend)) {
        stopFileSend(serialIoBusyStatus());
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

    const SerialIoResult result = serialPort_.writeBytes(chunk.bytes);
    if (!result.ok) {
        stopFileSend(utf8ToWide(result.errorMessage));
        handleSerialFailure(result.errorMessage);
        return;
    }

    saveRawEvent("Tx", chunk.bytes);
    appendPayloadLog(NativeLogKind::Tx, chunk.bytes);
    fileSend_.markBytesWritten(result.byteCount);
    statusCountersState_.addTxBytes(static_cast<std::uint64_t>(result.byteCount));
    updateFileSendProgress();
    updateStatusSegments();

    if (fileSend_.done()) {
        const std::wstring summary = uiString(T::FileSendDonePrefix) + std::to_wstring(fileSend_.sentBytes()) + uiString(T::BytesSuffix);
        stopFileSend(summary);
        return;
    }

    if ((fileSend_.sentBytes() % static_cast<std::uintmax_t>(kNativeFileSendChunkBytes * 16)) == 0) {
        setStatus(uiString(T::FileSendProgressPrefix)
            + std::to_wstring(fileSend_.sentBytes())
            + L"/"
            + std::to_wstring(fileSend_.totalBytes())
            + uiString(T::BytesSuffix));
    }
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
