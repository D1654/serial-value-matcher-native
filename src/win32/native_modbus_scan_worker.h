#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/modbus_core.h"
#include "native_storage/native_session_store.h"
#include "win32/native_log_model.h"
#include "transport/serial_transport.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace svm::win32 {

inline constexpr UINT kNativeModbusScanDoneMessage = WM_APP + 14;
inline constexpr UINT kNativeModbusScanProgressMessage = WM_APP + 15;
inline constexpr UINT kNativeModbusScanDataMessage = WM_APP + 16;

struct NativeModbusScanDataBatch {
    std::vector<native_storage::RawIoEvent> rawEvents;
    std::vector<NativeLogEntry> logEntries;
};

struct NativeModbusScanResult {
    native_storage::ScanExecutionRecord execution;
    std::string errorMessage;
    bool serialFailed = false;
    bool cancelled = false;
};

struct NativeModbusScanProgress {
    std::size_t completedBlocks = 0;
    std::size_t totalBlocks = 0;
    std::size_t successBlocks = 0;
    std::size_t failedBlocks = 0;
    std::size_t observations = 0;
};

struct NativeModbusScanContext {
    HWND notifyWindow = nullptr;
    transport::SerialTransport* serialTransport = nullptr;
    std::atomic_bool* cancelRequested = nullptr;
    core::modbus::ScanPlan plan;
    native_storage::ScanExecutionRecord execution;
    std::string scanSessionId;
    std::string timeoutErrorMessage;
    std::string threadExceptionMessage;
};

DWORD WINAPI nativeModbusScanThreadProc(void* parameter);

} // namespace svm::win32

#endif
