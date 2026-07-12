#include "win32/main_window.h"

#if defined(_WIN32)

#include "win32/native_time_utils.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace svm::win32 {

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

void NativeMainWindow::saveRawEvent(std::string direction, const std::vector<std::uint8_t>& payload) {
    if (!store_.isOpen()) {
        return;
    }
    native_storage::RawIoEvent event;
    event.sessionId = sessionId_;
    event.direction = std::move(direction);
    event.timestampUtc = nativeUtcTimestampText();
    event.endpoint = serialTransport_.endpoint();
    event.payload = payload;
    saveRawEvents({std::move(event)});
}

bool NativeMainWindow::saveRawEvents(std::vector<native_storage::RawIoEvent> events) {
    if (!store_.isOpen() || events.empty()) {
        return false;
    }
    return store_.appendRawEvents(events);
}

} // namespace svm::win32

#endif
