#include "core/modbus_scan_executor_core.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <utility>

namespace svm::core::modbus {
namespace {

Text defaultUtcTimestampText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

Text nowUtc(const ScanExecutionOptions& options) {
    if (options.nowUtc) {
        return options.nowUtc();
    }
    return defaultUtcTimestampText();
}

Text invalidPlanMessage(const ScanPlan& plan, const ScanExecutionOptions& options) {
    if (plan.slaveId < 1 || plan.slaveId > 247) {
        return "Scan execution failed: slave id must be 1-247.";
    }

    if (!isSupportedReadFunction(plan.functionCode)) {
        return "Scan execution failed: unsupported read function.";
    }

    if (plan.blocks.empty()) {
        return "Scan execution failed: scan plan has no request blocks.";
    }

    if (plan.retryCount < 0) {
        return "Scan execution failed: retry count must not be negative.";
    }

    if (options.responseTimeoutMs < 0) {
        return "Scan execution failed: response timeout must not be negative.";
    }

    for (const auto& block : plan.blocks) {
        if (block.requestFrame.empty()) {
            return "Scan execution failed: request block is missing its raw request frame.";
        }

        if (block.startAddress < 0 || block.endAddress < block.startAddress || block.quantity <= 0) {
            return "Scan execution failed: request block address or quantity is invalid.";
        }
    }

    return {};
}

bool shouldRetry(ScanAttemptStatus status, const ScanExecutionOptions& options) {
    switch (status) {
    case ScanAttemptStatus::Success:
        return false;
    case ScanAttemptStatus::Timeout:
        return options.retryOnTimeout;
    case ScanAttemptStatus::TransportError:
        return options.retryOnTransportError;
    case ScanAttemptStatus::ParseError:
        return options.retryOnParseError;
    case ScanAttemptStatus::ModbusException:
        return options.retryOnModbusException;
    }

    return false;
}

ScanAttemptResult attemptFromExchange(
    const ScanBlock& block,
    int attemptIndex,
    ByteSpan fallbackRequestFrame,
    const RtuTransportExchange& exchange) {
    ScanAttemptResult attempt;
    attempt.blockIndex = block.index;
    attempt.attemptIndex = attemptIndex;
    attempt.requestFrame = exchange.requestFrame.empty()
        ? ByteBuffer(fallbackRequestFrame.begin(), fallbackRequestFrame.end())
        : exchange.requestFrame;
    attempt.responseFrame = exchange.responseFrame;
    attempt.errorMessage = exchange.errorMessage;
    attempt.sentAtUtc = exchange.sentAtUtc;
    attempt.receivedAtUtc = exchange.receivedAtUtc;
    attempt.endpoint = exchange.endpoint;

    switch (exchange.status) {
    case RtuTransportExchangeStatus::Success:
        attempt.status = ScanAttemptStatus::ParseError;
        break;
    case RtuTransportExchangeStatus::Timeout:
        attempt.status = ScanAttemptStatus::Timeout;
        if (attempt.errorMessage.empty()) {
            attempt.errorMessage = "Waiting for Modbus response timed out.";
        }
        break;
    case RtuTransportExchangeStatus::TransportError:
        attempt.status = ScanAttemptStatus::TransportError;
        if (attempt.errorMessage.empty()) {
            attempt.errorMessage = "Modbus transport failed.";
        }
        break;
    }

    return attempt;
}

Text bestObservedAtUtc(const ScanAttemptResult& attempt, const ScanExecutionOptions& options) {
    if (!attempt.receivedAtUtc.empty()) {
        return attempt.receivedAtUtc;
    }
    if (!attempt.sentAtUtc.empty()) {
        return attempt.sentAtUtc;
    }
    return nowUtc(options);
}

bool isCancellationRequested(const ScanExecutionOptions& options) {
    return options.shouldCancel && options.shouldCancel();
}

void publishProgress(const ScanPlan& plan, const ScanExecutionResult& result, const ScanExecutionOptions& options) {
    if (!options.onProgress) {
        return;
    }

    options.onProgress(ScanExecutionProgress{
        static_cast<int>(result.blocks.size()),
        static_cast<int>(plan.blocks.size()),
        result.successBlockCount,
        result.failedBlockCount,
        static_cast<int>(result.observations.size())
    });
}

void sleepBeforeNextBlock(const ScanPlan& plan, const ScanExecutionOptions& options, std::size_t nextBlockIndex) {
    if (!options.sleepForMs || plan.requestIntervalMs <= 0 || nextBlockIndex >= plan.blocks.size()) {
        return;
    }
    options.sleepForMs(plan.requestIntervalMs);
}

} // namespace

Text describeScanAttemptStatus(ScanAttemptStatus status) {
    switch (status) {
    case ScanAttemptStatus::Success:
        return "success";
    case ScanAttemptStatus::ModbusException:
        return "Modbus exception";
    case ScanAttemptStatus::ParseError:
        return "response parse error";
    case ScanAttemptStatus::Timeout:
        return "response timeout";
    case ScanAttemptStatus::TransportError:
        return "transport error";
    }

    return "unknown status";
}

Text describeScanExecutionStatus(ScanExecutionStatus status) {
    switch (status) {
    case ScanExecutionStatus::Completed:
        return "completed";
    case ScanExecutionStatus::CompletedWithErrors:
        return "completed with errors";
    case ScanExecutionStatus::Cancelled:
        return "cancelled";
    case ScanExecutionStatus::Failed:
        return "failed";
    }

    return "unknown status";
}

ScanExecutor::ScanExecutor(RtuTransport& transport)
    : transport_(transport) {}

ScanExecutionResult ScanExecutor::execute(const ScanPlan& plan, const ScanExecutionOptions& options) {
    ScanExecutionResult result;
    result.startedAtUtc = nowUtc(options);
    result.plan = plan;

    const Text planError = invalidPlanMessage(plan, options);
    if (!planError.empty()) {
        result.errorMessage = planError;
        result.finishedAtUtc = nowUtc(options);
        return result;
    }

    result.blocks.reserve(plan.blocks.size());
    result.observations.reserve(static_cast<std::size_t>(plan.registerCount()));
    bool cancelled = false;

    auto markCancelled = [&cancelled, &result]() {
        cancelled = true;
        result.errorMessage = "Scan was cancelled.";
    };

    for (std::size_t blockPosition = 0; blockPosition < plan.blocks.size(); ++blockPosition) {
        const auto& block = plan.blocks[blockPosition];
        if (isCancellationRequested(options)) {
            markCancelled();
            publishProgress(plan, result, options);
            break;
        }

        ScanBlockResult blockResult;
        blockResult.block = block;
        const int maxAttempts = plan.retryCount + 1;
        blockResult.attempts.reserve(static_cast<std::size_t>(maxAttempts));

        for (int attemptIndex = 0; attemptIndex < maxAttempts; ++attemptIndex) {
            if (isCancellationRequested(options)) {
                markCancelled();
                break;
            }

            const auto exchange = transport_.exchange(block.requestFrame, options.responseTimeoutMs);
            auto attempt = attemptFromExchange(block, attemptIndex, block.requestFrame, exchange);

            if (exchange.status == RtuTransportExchangeStatus::Success) {
                const auto parsed = parseReadResponse(
                    exchange.responseFrame,
                    plan.slaveId,
                    plan.functionCode,
                    block.startAddress,
                    block.quantity);

                if (parsed.ok) {
                    attempt.status = ScanAttemptStatus::Success;
                    attempt.errorMessage.clear();
                    blockResult.ok = true;

                    const Text observedAtUtc = bestObservedAtUtc(attempt, options);
                    blockResult.observations.reserve(parsed.observations.size());
                    for (const auto& observation : parsed.observations) {
                        blockResult.observations.push_back(ScanObservation{
                            block.index,
                            attemptIndex,
                            plan.slaveId,
                            plan.functionCode,
                            observation.address,
                            observation.value,
                            observedAtUtc
                        });
                    }
                } else if (parsed.isException) {
                    attempt.status = ScanAttemptStatus::ModbusException;
                    attempt.isModbusException = true;
                    attempt.exceptionCode = parsed.exceptionCode;
                    attempt.exceptionDescription = parsed.exceptionDescription;
                    attempt.errorMessage = parsed.errorMessage;
                } else {
                    attempt.status = ScanAttemptStatus::ParseError;
                    attempt.errorMessage = parsed.errorMessage;
                }
            }

            const bool success = attempt.status == ScanAttemptStatus::Success;
            const bool retryAllowed = attemptIndex + 1 < maxAttempts && shouldRetry(attempt.status, options);
            if (!success && attempt.errorMessage.empty()) {
                attempt.errorMessage = describeScanAttemptStatus(attempt.status);
            }

            blockResult.finalErrorMessage = attempt.errorMessage;
            blockResult.attempts.push_back(std::move(attempt));

            if (!success && isCancellationRequested(options)) {
                markCancelled();
                break;
            }
            if (success || !retryAllowed) {
                break;
            }
        }

        if (cancelled && blockResult.attempts.empty()) {
            break;
        }

        if (blockResult.ok) {
            ++result.successBlockCount;
            result.observations.insert(result.observations.end(), blockResult.observations.begin(), blockResult.observations.end());
        } else {
            ++result.failedBlockCount;
            if (result.errorMessage.empty()) {
                result.errorMessage = blockResult.finalErrorMessage;
            }
        }

        result.blocks.push_back(std::move(blockResult));
        publishProgress(plan, result, options);

        if (cancelled || (!result.blocks.back().ok && !options.continueOnBlockError)) {
            break;
        }

        sleepBeforeNextBlock(plan, options, blockPosition + 1);
    }

    if (cancelled) {
        result.status = ScanExecutionStatus::Cancelled;
    } else if (result.failedBlockCount == 0) {
        result.status = ScanExecutionStatus::Completed;
        result.errorMessage.clear();
    } else if (result.successBlockCount > 0) {
        result.status = ScanExecutionStatus::CompletedWithErrors;
    } else {
        result.status = ScanExecutionStatus::Failed;
    }

    result.finishedAtUtc = nowUtc(options);
    return result;
}

} // namespace svm::core::modbus
