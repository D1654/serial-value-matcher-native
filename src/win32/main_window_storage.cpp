#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_time_utils.h"
#include "win32/utf8_win32.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace svm::win32 {
namespace {

std::string rawEventDirection(svm::transport::SerialDataDirection direction) {
    switch (direction) {
    case svm::transport::SerialDataDirection::Transmit:
        return "Tx";
    case svm::transport::SerialDataDirection::Receive:
        return "Rx";
    case svm::transport::SerialDataDirection::None:
        return "None";
    }
    return "None";
}

} // namespace

std::filesystem::path NativeMainWindow::defaultStoreDirectory() const {
    wchar_t localAppData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(localAppData) / L"SerialValueMatcherNative" / L"native-store";
    }
    wchar_t tempPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tempPath);
    return std::filesystem::path(tempPath) / L"SerialValueMatcherNative" / L"native-store";
}

native_storage::RawIoEvent NativeMainWindow::makeRawSerialEvent(
    const svm::transport::SerialOperationResult& result,
    const std::vector<std::uint8_t>& payload) const {
    native_storage::RawIoEvent event;
    event.sessionId = sessionId_;
    event.direction = rawEventDirection(
        svm::transport::serialOperationDirection(result.operation.kind));
    event.timestampUtc = nativeUtcTimestampText();
    event.endpoint = result.endpoint;
    event.payload = payload;
    event.operation = svm::transport::serialOperationKindName(result.operation.kind);
    event.requestId = result.operation.requestId;
    event.generation = result.operation.generation;
    event.status = svm::transport::serialOperationStatusName(result.status);
    event.deadlineStatus = svm::transport::serialDeadlineStatusName(result.deadlineStatus);
    event.byteCount = result.byteCount;
    event.errorCategory = svm::transport::serialErrorCategoryName(result.error.category);
    event.nativeCode = result.error.nativeCode;
    event.commErrorMask = result.error.commErrorMask;
    event.inputQueueBytes = result.error.inputQueueBytes;
    event.outputQueueBytes = result.error.outputQueueBytes;
    return event;
}

bool NativeMainWindow::saveRawEvent(
    const svm::transport::SerialOperationResult& result,
    const std::vector<std::uint8_t>& payload) {
    return saveRawEvents({makeRawSerialEvent(result, payload)});
}

bool NativeMainWindow::saveRawEvents(std::vector<native_storage::RawIoEvent> events) {
    if (events.empty()) {
        return true;
    }
    if (!store_.isOpen() || !store_.appendRawEvents(events)) {
        reportStorageFailure(L"保存原始通信证据");
        return false;
    }
    return true;
}

void NativeMainWindow::reportStorageFailure(std::wstring_view operation) {
    const std::wstring detail = utf8ToWide(store_.lastErrorText());
    const std::wstring message = std::wstring(L"存储失败：")
        + std::wstring(operation)
        + (detail.empty() ? L"。" : L"：" + detail);
    if (!storageFailureReported_) {
        storageFailureReported_ = true;
        appendLog(NativeLogKind::Error, message);
    }
    if (shuttingDown_ && !shutdownStorageFailureShown_) {
        shutdownStorageFailureShown_ = true;
        MessageBoxW(nullptr, message.c_str(), L"存储失败", MB_ICONERROR | MB_OK | MB_TASKMODAL);
    }
    setStatus(message);
}

} // namespace svm::win32

#endif
