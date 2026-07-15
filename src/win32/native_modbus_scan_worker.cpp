#include "win32/native_modbus_scan_worker.h"

#if defined(_WIN32)

#include "core/modbus_scan_executor_core.h"
#include "transport/serial_rtu_transport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <utility>

namespace svm::win32 {
namespace {

constexpr ULONGLONG kModbusProgressMinIntervalMs = 80;
constexpr std::size_t kModbusDataBatchMinItems = 24;
constexpr const char* kNativeModbusCancelledMessage = "扫描已取消。";

std::wstring localClockText() {
    SYSTEMTIME now = {};
    GetLocalTime(&now);
    wchar_t buffer[32] = {};
    swprintf_s(
        buffer,
        L"%02u:%02u:%02u.%03u",
        static_cast<unsigned int>(now.wHour),
        static_cast<unsigned int>(now.wMinute),
        static_cast<unsigned int>(now.wSecond),
        static_cast<unsigned int>(now.wMilliseconds));
    return buffer;
}

std::string timestampText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
    gmtime_s(&utc, &time);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string statusText(core::modbus::ScanAttemptStatus status, const std::string& errorMessage) {
    if (errorMessage == kNativeModbusCancelledMessage) {
        return "cancelled";
    }

    switch (status) {
    case core::modbus::ScanAttemptStatus::Success:
        return "success";
    case core::modbus::ScanAttemptStatus::ModbusException:
    case core::modbus::ScanAttemptStatus::ParseError:
        return "parse-error";
    case core::modbus::ScanAttemptStatus::Timeout:
        return "timeout";
    case core::modbus::ScanAttemptStatus::TransportError:
        return "transport-error";
    }

    return "unknown";
}

void applyExecutionResult(
    const std::string& scanSessionId,
    const std::string& fallbackEndpoint,
    const core::modbus::ScanExecutionResult& source,
    native_storage::ScanExecutionRecord& target) {
    target.session.successBlockCount = source.successBlockCount;
    target.session.failedBlockCount = source.failedBlockCount;
    target.session.finishedAtUtc = source.finishedAtUtc.empty() ? timestampText() : source.finishedAtUtc;
    target.session.errorMessage = source.errorMessage;
    target.attempts.clear();
    target.observations.clear();

    for (const core::modbus::ScanBlockResult& block : source.blocks) {
        for (const core::modbus::ScanAttemptResult& attempt : block.attempts) {
            native_storage::ScanAttemptRecord record;
            record.sessionId = scanSessionId;
            record.blockIndex = attempt.blockIndex;
            record.attemptIndex = attempt.attemptIndex;
            record.startAddress = block.block.startAddress;
            record.quantity = block.block.quantity;
            record.status = statusText(attempt.status, attempt.errorMessage);
            record.requestFrame = attempt.requestFrame;
            record.responseFrame = attempt.responseFrame;
            record.errorMessage = attempt.errorMessage;
            record.isModbusException = attempt.isModbusException;
            record.exceptionCode = attempt.exceptionCode;
            record.exceptionDescription = attempt.exceptionDescription;
            record.sentAtUtc = attempt.sentAtUtc;
            record.receivedAtUtc = attempt.receivedAtUtc;
            record.endpoint = attempt.endpoint.empty() ? fallbackEndpoint : attempt.endpoint;
            target.attempts.push_back(std::move(record));
        }
    }

    for (const core::modbus::ScanObservation& observation : source.observations) {
        native_storage::ScanObservationRecord record;
        record.sessionId = scanSessionId;
        record.blockIndex = observation.blockIndex;
        record.attemptIndex = observation.attemptIndex;
        record.slaveId = observation.slaveId;
        record.functionCode = observation.functionCode;
        record.address = observation.address;
        record.value = observation.value;
        record.observedAtUtc = observation.observedAtUtc;
        target.observations.push_back(std::move(record));
    }
}

} // namespace

DWORD WINAPI nativeModbusScanThreadProc(void* parameter) {
    std::unique_ptr<NativeModbusScanContext> context(static_cast<NativeModbusScanContext*>(parameter));
    if (!context
        || context->notifyWindow == nullptr
        || context->byteStream == nullptr
        || context->generation == transport::kUnassignedSerialSessionGeneration
        || context->endpoint.empty()
        || !context->generationIsCurrent
        || context->cancelRequested == nullptr
        || context->terminalResult == nullptr) {
        return 1;
    }

    auto* result = new NativeModbusScanResult;
    result->generation = context->generation;
    result->execution = std::move(context->execution);
    const std::string scanSessionId = context->scanSessionId;
    const std::string endpoint = context->endpoint;
    NativeModbusScanDataBatch dataBatch;
    dataBatch.generation = context->generation;

    const auto generationCurrent = [&]() {
        return context->generationIsCurrent(context->generation);
    };

    const auto postDataBatch = [&](bool force = false, bool allowInactiveGeneration = false) {
        const std::size_t itemCount = dataBatch.rawEvents.size() + dataBatch.logEntries.size();
        if ((!generationCurrent() && !allowInactiveGeneration)
            || itemCount == 0
            || (!force && itemCount < kModbusDataBatchMinItems)) {
            return;
        }
        auto* postedBatch = new NativeModbusScanDataBatch(std::move(dataBatch));
        dataBatch = {};
        dataBatch.generation = context->generation;
        if (!PostMessageW(context->notifyWindow, kNativeModbusScanDataMessage, 0, reinterpret_cast<LPARAM>(postedBatch))) {
            delete postedBatch;
        }
    };

    const auto appendRawEvent = [&](std::string direction, const std::vector<std::uint8_t>& payload) {
        native_storage::RawIoEvent event;
        event.sessionId = scanSessionId;
        event.direction = std::move(direction);
        event.timestampUtc = timestampText();
        event.endpoint = endpoint;
        event.payload = payload;
        dataBatch.rawEvents.push_back(std::move(event));
        postDataBatch();
    };
    const auto appendPayloadEntry = [&](NativeLogKind kind, const wchar_t* prefix, const std::vector<std::uint8_t>& payload) {
        NativeLogEntry entry;
        entry.kind = kind;
        entry.timestamp = localClockText();
        entry.payloadPrefix = prefix;
        entry.payload = payload;
        entry.hasPayload = true;
        dataBatch.logEntries.push_back(std::move(entry));
        postDataBatch();
    };
    const auto appendFrame = [&](bool tx, const std::vector<std::uint8_t>& payload) {
        appendRawEvent(tx ? "Tx" : "Rx", payload);
        appendPayloadEntry(tx ? NativeLogKind::ModbusTx : NativeLogKind::ModbusRx, tx ? L"[Modbus TX]" : L"[Modbus RX]", payload);
    };

    ULONGLONG lastProgressPostTick = 0;
    const auto postProgress = [&](std::size_t completedBlocks,
                                  std::size_t totalBlocks,
                                  std::size_t successBlocks,
                                  std::size_t failedBlocks,
                                  std::size_t observations,
                                  bool force = false,
                                  bool allowInactiveGeneration = false) {
        if (!generationCurrent() && !allowInactiveGeneration) {
            return;
        }
        const ULONGLONG now = GetTickCount64();
        const bool complete = completedBlocks >= totalBlocks;
        if (!force
            && !complete
            && lastProgressPostTick != 0
            && now - lastProgressPostTick < kModbusProgressMinIntervalMs) {
            return;
        }
        lastProgressPostTick = now;
        auto* progress = new NativeModbusScanProgress;
        progress->generation = context->generation;
        progress->completedBlocks = completedBlocks;
        progress->totalBlocks = totalBlocks;
        progress->successBlocks = successBlocks;
        progress->failedBlocks = failedBlocks;
        progress->observations = observations;
        if (!PostMessageW(context->notifyWindow, kNativeModbusScanProgressMessage, 0, reinterpret_cast<LPARAM>(progress))) {
            delete progress;
        }
    };

    try {
        transport::SerialRtuTransport serialTransport(
            *context->byteStream,
            {
                .generation = context->generation,
                .endpoint = endpoint,
                .generationIsCurrent = [&](transport::SerialSessionGeneration generation) {
                    return generation == context->generation && generationCurrent();
                },
                .shouldCancel = [&]() {
                    return context->cancelRequested->load(std::memory_order_relaxed);
                },
                .nowUtc = timestampText,
                .onFrame = appendFrame,
                .timeoutErrorMessage = context->timeoutErrorMessage,
            });
        core::modbus::ScanExecutor executor(serialTransport);
        core::modbus::ScanExecutionOptions options;
        options.responseTimeoutMs = 1200;
        options.shouldCancel = [&]() {
            return context->cancelRequested->load(std::memory_order_relaxed)
                || serialTransport.cancelObserved()
                || (serialTransport.serialFailed() && !generationCurrent());
        };
        options.sleepForMs = [](int delayMs) {
            Sleep(static_cast<DWORD>(std::max(0, delayMs)));
        };
        options.nowUtc = timestampText;
        options.onProgress = [&](const core::modbus::ScanExecutionProgress& progress) {
            postProgress(
                static_cast<std::size_t>(std::max(0, progress.completedBlocks)),
                static_cast<std::size_t>(std::max(0, progress.totalBlocks)),
                static_cast<std::size_t>(std::max(0, progress.successBlocks)),
                static_cast<std::size_t>(std::max(0, progress.failedBlocks)),
                static_cast<std::size_t>(std::max(0, progress.observations)));
        };

        const core::modbus::ScanExecutionResult executionResult = executor.execute(context->plan, options);
        applyExecutionResult(scanSessionId, endpoint, executionResult, result->execution);

        result->cancelled = serialTransport.cancelObserved() || context->cancelRequested->load(std::memory_order_relaxed);
        result->serialFailed = serialTransport.serialFailed();
        if (result->serialFailed) {
            result->errorMessage = serialTransport.lastErrorMessage().empty()
                ? executionResult.errorMessage
                : serialTransport.lastErrorMessage();
        } else if (!executionResult.errorMessage.empty()) {
            result->errorMessage = executionResult.errorMessage;
        }

        if (result->cancelled) {
            result->execution.session.status = "cancelled";
        } else {
            switch (executionResult.status) {
            case core::modbus::ScanExecutionStatus::Completed:
                result->execution.session.status = "completed";
                break;
            case core::modbus::ScanExecutionStatus::CompletedWithErrors:
                result->execution.session.status = result->execution.session.successBlockCount > 0 ? "partial" : "failed";
                break;
            case core::modbus::ScanExecutionStatus::Failed:
                result->execution.session.status = "failed";
                break;
            }
        }
        result->execution.session.errorMessage = result->errorMessage;
    } catch (...) {
        result->execution.session.finishedAtUtc = timestampText();
        result->execution.session.status = "failed";
        result->errorMessage = context->threadExceptionMessage;
        result->execution.session.errorMessage = result->errorMessage;
    }

    postProgress(
        static_cast<std::size_t>(std::max(0, result->execution.session.successBlockCount + result->execution.session.failedBlockCount)),
        context->plan.blocks.size(),
        static_cast<std::size_t>(std::max(0, result->execution.session.successBlockCount)),
        static_cast<std::size_t>(std::max(0, result->execution.session.failedBlockCount)),
        result->execution.observations.size(),
        true,
        result->serialFailed);
    postDataBatch(true, result->serialFailed);
    NativeModbusScanResult* displaced = context->terminalResult->exchange(result, std::memory_order_acq_rel);
    delete displaced;
    PostMessageW(context->notifyWindow, kNativeModbusScanDoneMessage, 0, 0);
    return 0;
}

} // namespace svm::win32

#endif
