#include "win32/native_modbus_scan_worker.h"

#if defined(_WIN32)

#include "core/modbus_scan_executor_core.h"

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

std::vector<std::uint8_t> bytesFromSpan(core::ByteSpan bytes) {
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

std::size_t expectedNormalResponseLength(core::ByteSpan requestFrame) {
    if (requestFrame.size() >= 6) {
        const std::size_t quantity = (static_cast<std::size_t>(requestFrame[4]) << 8U)
            | static_cast<std::size_t>(requestFrame[5]);
        if (quantity > 0 && quantity <= 125) {
            return 5 + quantity * 2;
        }
    }
    return 5;
}

bool responseLooksComplete(const std::vector<std::uint8_t>& response, std::size_t expectedNormalBytes) {
    if (response.size() >= 5 && (response[1] & 0x80U) != 0) {
        return true;
    }
    return response.size() >= expectedNormalBytes;
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

class NativeWin32ModbusTransport final : public core::modbus::RtuTransport {
public:
    using FrameCallback = std::function<void(bool tx, const std::vector<std::uint8_t>& payload)>;

    NativeWin32ModbusTransport(
        Win32SerialPort& serialPort,
        std::atomic_bool& cancelRequested,
        std::string endpoint,
        std::string timeoutErrorMessage,
        FrameCallback frameCallback)
        : serialPort_(serialPort),
          cancelRequested_(cancelRequested),
          endpoint_(std::move(endpoint)),
          timeoutErrorMessage_(std::move(timeoutErrorMessage)),
          frameCallback_(std::move(frameCallback)) {}

    core::modbus::RtuTransportExchange exchange(core::ByteSpan requestFrame, int responseTimeoutMs) override {
        core::modbus::RtuTransportExchange exchange;
        exchange.requestFrame = core::ByteBuffer(requestFrame.begin(), requestFrame.end());
        exchange.endpoint = endpoint_;
        exchange.sentAtUtc = timestampText();

        if (cancelRequested_.load(std::memory_order_relaxed)) {
            cancelObserved_ = true;
            exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
            exchange.errorMessage = kNativeModbusCancelledMessage;
            exchange.receivedAtUtc = timestampText();
            return exchange;
        }

        if (requestFrame.empty()) {
            exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
            exchange.errorMessage = "Modbus 请求为空。";
            return exchange;
        }

        const std::vector<std::uint8_t> request = bytesFromSpan(requestFrame);
        const SerialIoResult writeResult = serialPort_.writeBytes(request);
        if (!writeResult.ok) {
            serialFailed_ = true;
            lastErrorMessage_ = writeResult.errorMessage;
            exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
            exchange.errorMessage = writeResult.errorMessage;
            return exchange;
        }

        if (frameCallback_) {
            frameCallback_(true, request);
        }

        exchange.status = core::modbus::RtuTransportExchangeStatus::Timeout;
        std::vector<std::uint8_t> response;
        const std::size_t expectedNormalBytes = expectedNormalResponseLength(requestFrame);
        const int timeoutMs = std::max(0, responseTimeoutMs);
        const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
        while (GetTickCount64() < deadline) {
            if (cancelRequested_.load(std::memory_order_relaxed)) {
                cancelObserved_ = true;
                exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
                exchange.errorMessage = kNativeModbusCancelledMessage;
                break;
            }

            const ULONGLONG now = GetTickCount64();
            const DWORD waitMs = static_cast<DWORD>(std::min<ULONGLONG>(50, deadline > now ? deadline - now : 0));
            if (waitMs == 0) {
                break;
            }

            if (serialPort_.waitForReadyRead(static_cast<int>(waitMs))) {
                std::vector<std::uint8_t> chunk = serialPort_.readAvailable(260);
                response.insert(response.end(), chunk.begin(), chunk.end());
                if (responseLooksComplete(response, expectedNormalBytes)) {
                    exchange.status = core::modbus::RtuTransportExchangeStatus::Success;
                    break;
                }
            } else {
                const std::string error = serialPort_.lastErrorText();
                if (!error.empty()) {
                    serialFailed_ = true;
                    lastErrorMessage_ = error;
                    exchange.status = core::modbus::RtuTransportExchangeStatus::TransportError;
                    exchange.errorMessage = error;
                    break;
                }
            }
        }

        exchange.receivedAtUtc = timestampText();
        exchange.responseFrame = std::move(response);
        if (!exchange.responseFrame.empty() && frameCallback_) {
            frameCallback_(false, exchange.responseFrame);
        }

        if (exchange.status == core::modbus::RtuTransportExchangeStatus::Success
            || exchange.status == core::modbus::RtuTransportExchangeStatus::TransportError) {
            return exchange;
        }

        exchange.status = core::modbus::RtuTransportExchangeStatus::Timeout;
        exchange.errorMessage = timeoutErrorMessage_.empty()
            ? std::string("等待 Modbus 响应超时。")
            : timeoutErrorMessage_;
        return exchange;
    }

    bool serialFailed() const noexcept { return serialFailed_; }
    bool cancelObserved() const noexcept { return cancelObserved_; }
    const std::string& lastErrorMessage() const noexcept { return lastErrorMessage_; }

private:
    Win32SerialPort& serialPort_;
    std::atomic_bool& cancelRequested_;
    std::string endpoint_;
    std::string timeoutErrorMessage_;
    FrameCallback frameCallback_;
    bool serialFailed_ = false;
    bool cancelObserved_ = false;
    std::string lastErrorMessage_;
};

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
    if (!context || context->notifyWindow == nullptr || context->serialPort == nullptr || context->cancelRequested == nullptr) {
        return 1;
    }

    auto* result = new NativeModbusScanResult;
    result->execution = std::move(context->execution);
    const std::string scanSessionId = context->scanSessionId;
    const std::string endpoint = context->serialPort->endpoint();
    NativeModbusScanDataBatch dataBatch;

    const auto postDataBatch = [&](bool force = false) {
        const std::size_t itemCount = dataBatch.rawEvents.size() + dataBatch.logEntries.size();
        if (itemCount == 0 || (!force && itemCount < kModbusDataBatchMinItems)) {
            return;
        }
        auto* postedBatch = new NativeModbusScanDataBatch(std::move(dataBatch));
        dataBatch = {};
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
                                  bool force = false) {
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
        NativeWin32ModbusTransport transport(
            *context->serialPort,
            *context->cancelRequested,
            endpoint,
            context->timeoutErrorMessage,
            appendFrame);
        core::modbus::ScanExecutor executor(transport);
        core::modbus::ScanExecutionOptions options;
        options.responseTimeoutMs = 1200;
        options.shouldCancel = [&]() {
            return context->cancelRequested->load(std::memory_order_relaxed);
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

        result->cancelled = transport.cancelObserved() || context->cancelRequested->load(std::memory_order_relaxed);
        result->serialFailed = transport.serialFailed();
        if (result->serialFailed) {
            result->errorMessage = transport.lastErrorMessage().empty() ? executionResult.errorMessage : transport.lastErrorMessage();
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
        true);
    postDataBatch(true);
    if (!PostMessageW(context->notifyWindow, kNativeModbusScanDoneMessage, 0, reinterpret_cast<LPARAM>(result))) {
        delete result;
    }
    return 0;
}

} // namespace svm::win32

#endif
