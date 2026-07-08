#pragma once

#include "core/modbus_core.h"

#include <functional>
#include <vector>

namespace svm::core::modbus {

enum class RtuTransportExchangeStatus {
    Success,
    Timeout,
    TransportError
};

struct RtuTransportExchange {
    RtuTransportExchangeStatus status = RtuTransportExchangeStatus::TransportError;
    ByteBuffer requestFrame;
    ByteBuffer responseFrame;
    Text errorMessage;
    Text sentAtUtc;
    Text receivedAtUtc;
    Text endpoint;
};

class RtuTransport {
public:
    virtual ~RtuTransport() = default;

    virtual RtuTransportExchange exchange(ByteSpan requestFrame, int responseTimeoutMs) = 0;
};

enum class ScanAttemptStatus {
    Success,
    ModbusException,
    ParseError,
    Timeout,
    TransportError
};

enum class ScanExecutionStatus {
    Completed,
    CompletedWithErrors,
    Failed
};

struct ScanAttemptResult {
    int blockIndex = -1;
    int attemptIndex = 0;
    ScanAttemptStatus status = ScanAttemptStatus::TransportError;
    ByteBuffer requestFrame;
    ByteBuffer responseFrame;
    Text errorMessage;
    bool isModbusException = false;
    Byte exceptionCode = 0;
    Text exceptionDescription;
    Text sentAtUtc;
    Text receivedAtUtc;
    Text endpoint;
};

struct ScanObservation {
    int blockIndex = -1;
    int attemptIndex = 0;
    int slaveId = 0;
    Byte functionCode = 0;
    std::uint16_t address = 0;
    std::uint16_t value = 0;
    Text observedAtUtc;
};

struct ScanBlockResult {
    ScanBlock block;
    std::vector<ScanAttemptResult> attempts;
    std::vector<ScanObservation> observations;
    bool ok = false;
    Text finalErrorMessage;
};

struct ScanExecutionProgress {
    int completedBlocks = 0;
    int totalBlocks = 0;
    int successBlocks = 0;
    int failedBlocks = 0;
    int observations = 0;
};

struct ScanExecutionOptions {
    int responseTimeoutMs = 1000;
    bool continueOnBlockError = true;
    bool retryOnTimeout = true;
    bool retryOnTransportError = true;
    bool retryOnParseError = false;
    bool retryOnModbusException = false;
    std::function<bool()> shouldCancel;
    std::function<void(int)> sleepForMs;
    std::function<Text()> nowUtc;
    std::function<void(const ScanExecutionProgress&)> onProgress;
};

struct ScanExecutionResult {
    ScanExecutionStatus status = ScanExecutionStatus::Failed;
    Text errorMessage;
    Text startedAtUtc;
    Text finishedAtUtc;
    ScanPlan plan;
    std::vector<ScanBlockResult> blocks;
    std::vector<ScanObservation> observations;
    int successBlockCount = 0;
    int failedBlockCount = 0;
};

Text describeScanAttemptStatus(ScanAttemptStatus status);
Text describeScanExecutionStatus(ScanExecutionStatus status);

class ScanExecutor {
public:
    explicit ScanExecutor(RtuTransport& transport);

    ScanExecutionResult execute(const ScanPlan& plan, const ScanExecutionOptions& options = {});

private:
    RtuTransport& transport_;
};

} // namespace svm::core::modbus
