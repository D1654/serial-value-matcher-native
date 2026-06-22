#include "win32/native_modbus_scan_worker.h"

#if defined(_WIN32)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>

namespace svm::win32 {
namespace {

constexpr ULONGLONG kModbusProgressMinIntervalMs = 80;

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

    const auto appendRawEvent = [&](std::string direction, const std::vector<std::uint8_t>& payload) {
        native_storage::RawIoEvent event;
        event.sessionId = scanSessionId;
        event.direction = std::move(direction);
        event.timestampUtc = timestampText();
        event.endpoint = endpoint;
        event.payload = payload;
        result->rawEvents.push_back(std::move(event));
    };
    const auto appendPayloadEntry = [&](NativeLogKind kind, const wchar_t* prefix, const std::vector<std::uint8_t>& payload) {
        NativeLogEntry entry;
        entry.kind = kind;
        entry.timestamp = localClockText();
        entry.payloadPrefix = prefix;
        entry.payload = payload;
        entry.hasPayload = true;
        result->logEntries.push_back(std::move(entry));
    };
    ULONGLONG lastProgressPostTick = 0;
    const auto postProgress = [&](bool force = false) {
        const ULONGLONG now = GetTickCount64();
        const bool complete = result->execution.attempts.size() >= context->plan.blocks.size();
        if (!force
            && !complete
            && lastProgressPostTick != 0
            && now - lastProgressPostTick < kModbusProgressMinIntervalMs) {
            return;
        }
        lastProgressPostTick = now;
        auto* progress = new NativeModbusScanProgress;
        progress->completedBlocks = result->execution.attempts.size();
        progress->totalBlocks = context->plan.blocks.size();
        progress->successBlocks = static_cast<std::size_t>(std::max(0, result->execution.session.successBlockCount));
        progress->failedBlocks = static_cast<std::size_t>(std::max(0, result->execution.session.failedBlockCount));
        progress->observations = result->execution.observations.size();
        if (!PostMessageW(context->notifyWindow, kNativeModbusScanProgressMessage, 0, reinterpret_cast<LPARAM>(progress))) {
            delete progress;
        }
    };

    try {
        for (const core::modbus::ScanBlock& block : context->plan.blocks) {
            if (*context->cancelRequested) {
                result->cancelled = true;
                postProgress(true);
                break;
            }

            native_storage::ScanAttemptRecord attempt;
            attempt.sessionId = scanSessionId;
            attempt.blockIndex = block.index;
            attempt.attemptIndex = 0;
            attempt.startAddress = block.startAddress;
            attempt.quantity = block.quantity;
            attempt.requestFrame = block.requestFrame;
            attempt.sentAtUtc = timestampText();
            attempt.endpoint = endpoint;

            const SerialIoResult writeResult = context->serialPort->writeBytes(block.requestFrame);
            if (!writeResult.ok) {
                attempt.status = "write-error";
                attempt.errorMessage = writeResult.errorMessage;
                result->execution.attempts.push_back(std::move(attempt));
                ++result->execution.session.failedBlockCount;
                result->errorMessage = writeResult.errorMessage;
                result->serialFailed = true;
                postProgress(true);
                break;
            }

            appendRawEvent("Tx", block.requestFrame);
            appendPayloadEntry(NativeLogKind::ModbusTx, L"[Modbus TX]", block.requestFrame);

            std::vector<std::uint8_t> response;
            const std::size_t expectedNormalBytes = static_cast<std::size_t>(5 + block.quantity * 2);
            const ULONGLONG deadline = GetTickCount64() + 1200;
            while (GetTickCount64() < deadline) {
                if (*context->cancelRequested) {
                    result->cancelled = true;
                    break;
                }
                if (context->serialPort->waitForReadyRead(50)) {
                    std::vector<std::uint8_t> chunk = context->serialPort->readAvailable(260);
                    response.insert(response.end(), chunk.begin(), chunk.end());
                    if (response.size() >= expectedNormalBytes
                        || (response.size() >= 5 && (response[1] & 0x80U) != 0)) {
                        break;
                    }
                } else if (!context->serialPort->lastErrorText().empty()) {
                    attempt.errorMessage = context->serialPort->lastErrorText();
                    result->errorMessage = attempt.errorMessage;
                    result->serialFailed = true;
                    break;
                }
            }

            attempt.receivedAtUtc = timestampText();
            attempt.responseFrame = response;
            if (!response.empty()) {
                appendRawEvent("Rx", response);
                appendPayloadEntry(NativeLogKind::ModbusRx, L"[Modbus RX]", response);
            }

            if (result->cancelled) {
                attempt.status = "cancelled";
                result->execution.attempts.push_back(std::move(attempt));
                postProgress(true);
                break;
            }
            if (result->serialFailed) {
                attempt.status = "read-error";
                result->execution.attempts.push_back(std::move(attempt));
                ++result->execution.session.failedBlockCount;
                postProgress(true);
                break;
            }
            if (response.empty()) {
                attempt.status = "timeout";
                if (attempt.errorMessage.empty()) {
                    attempt.errorMessage = context->timeoutErrorMessage;
                }
                ++result->execution.session.failedBlockCount;
                result->execution.attempts.push_back(std::move(attempt));
                postProgress();
                continue;
            }

            const auto parsed = core::modbus::parseReadResponse(
                response,
                context->plan.slaveId,
                context->plan.functionCode,
                block.startAddress,
                block.quantity);
            if (!parsed.ok) {
                attempt.status = "parse-error";
                attempt.errorMessage = parsed.errorMessage;
                attempt.isModbusException = parsed.isException;
                attempt.exceptionCode = parsed.exceptionCode;
                attempt.exceptionDescription = parsed.exceptionDescription;
                ++result->execution.session.failedBlockCount;
                result->execution.attempts.push_back(std::move(attempt));
                postProgress();
                continue;
            }

            attempt.status = "success";
            ++result->execution.session.successBlockCount;
            for (const core::modbus::RegisterObservation& observation : parsed.observations) {
                native_storage::ScanObservationRecord record;
                record.sessionId = scanSessionId;
                record.blockIndex = block.index;
                record.attemptIndex = 0;
                record.slaveId = parsed.slaveId;
                record.functionCode = parsed.functionCode;
                record.address = observation.address;
                record.value = observation.value;
                record.observedAtUtc = attempt.receivedAtUtc;
                result->execution.observations.push_back(std::move(record));
            }
            result->execution.attempts.push_back(std::move(attempt));
            postProgress();
            if (context->plan.requestIntervalMs > 0) {
                Sleep(static_cast<DWORD>(context->plan.requestIntervalMs));
            }
        }

        result->execution.session.finishedAtUtc = timestampText();
        if (result->cancelled) {
            result->execution.session.status = "cancelled";
        } else {
            result->execution.session.status = result->execution.session.failedBlockCount == 0
                ? "completed"
                : (result->execution.session.successBlockCount > 0 ? "partial" : "failed");
        }
    } catch (...) {
        result->execution.session.finishedAtUtc = timestampText();
        result->execution.session.status = "failed";
        result->errorMessage = context->threadExceptionMessage;
    }

    if (!PostMessageW(context->notifyWindow, kNativeModbusScanDoneMessage, 0, reinterpret_cast<LPARAM>(result))) {
        delete result;
    }
    return 0;
}

} // namespace svm::win32

#endif
