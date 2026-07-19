#include "win32/win32_serial_session.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kFaultWaitMaxMs = svm::transport::kSerialTerminalResultTargetMs / 2;

struct ModbusOracle {
    std::vector<std::uint8_t> request;
    std::vector<std::uint8_t> response;
};

const ModbusOracle& oracleFor(std::size_t transactionIndex) {
    static const std::array<ModbusOracle, 2> oracles = {
        ModbusOracle{
            .request = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B},
            .response = {0x01, 0x03, 0x04, 0x41, 0x48, 0x00, 0x00, 0x7B, 0xF3},
        },
        ModbusOracle{
            .request = {0x01, 0x03, 0x00, 0x02, 0x00, 0x02, 0x65, 0xCB},
            .response = {0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78, 0x81, 0x07},
        },
    };
    return oracles[transactionIndex % oracles.size()];
}

const std::vector<std::uint8_t>& cancelMarker() {
    static const std::vector<std::uint8_t> marker = {
        0x7E, 0x43, 0x41, 0x4E, 0x43, 0x45, 0x4C, 0x7F,
    };
    return marker;
}

void printBytes(std::ostream& output, const std::vector<std::uint8_t>& bytes) {
    for (std::uint8_t byte : bytes) {
        static constexpr char digits[] = "0123456789ABCDEF";
        output << digits[(byte >> 4) & 0x0F] << digits[byte & 0x0F] << ' ';
    }
}

bool parsePositiveIntEnv(const char* name, int defaultValue, int maxValue, int& output) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        output = defaultValue;
        return true;
    }

    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1 || parsed > maxValue) {
        std::cerr << name << " must be an integer in [1, " << maxValue << "], got: " << value << '\n';
        return false;
    }

    output = static_cast<int>(parsed);
    return true;
}

svm::transport::SerialDeadline deadlineAfter(int timeoutMs) {
    return {
        .expiresAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs),
    };
}

void printOperationEvidence(const svm::transport::SerialOperationResult& result) {
    std::cerr << " request-id=" << result.operation.requestId
              << " generation=" << result.operation.generation
              << " status=" << static_cast<int>(result.status)
              << " category=" << static_cast<int>(result.error.category)
              << " native-code=" << result.error.nativeCode
              << " bytes=" << result.byteCount;
}

bool operationBelongsToGeneration(
    const svm::transport::SerialOperationResult& result,
    svm::transport::SerialOperationKind kind,
    svm::transport::SerialSessionGeneration generation,
    const char* scenarioName,
    int transactionIndex) {
    if (result.operation.assigned()
        && result.operation.kind == kind
        && result.operation.generation == generation) {
        return true;
    }
    std::cerr << "operation identity mismatch scenario=" << scenarioName
              << " transaction=" << transactionIndex;
    printOperationEvidence(result);
    std::cerr << '\n';
    return false;
}

bool readExpectedBytes(
    svm::transport::SerialByteStream& stream,
    std::size_t byteCount,
    const svm::transport::SerialDeadline& deadline,
    svm::transport::SerialSessionGeneration generation,
    const char* scenarioName,
    int transactionIndex,
    std::vector<std::uint8_t>& bytes) {
    while (bytes.size() < byteCount) {
        if (deadline.expiresAt.has_value()
            && std::chrono::steady_clock::now() >= *deadline.expiresAt) {
            break;
        }

        svm::transport::SerialReadResult read = stream.readAvailable(byteCount - bytes.size(), deadline);
        if (!operationBelongsToGeneration(
                read.operation,
                svm::transport::SerialOperationKind::Read,
                generation,
                scenarioName,
                transactionIndex)) {
            return false;
        }
        if (read.operation.status == svm::transport::SerialOperationStatus::Timeout) {
            break;
        }
        if (!read.operation.succeeded()) {
            std::cerr << "read failed scenario=" << scenarioName
                      << " transaction=" << transactionIndex;
            printOperationEvidence(read.operation);
            std::cerr << '\n';
            return false;
        }
        if (read.operation.byteCount != read.bytes.size()
            || read.operation.error.byteCount != read.bytes.size()) {
            std::cerr << "read byte evidence mismatch scenario=" << scenarioName
                      << " transaction=" << transactionIndex;
            printOperationEvidence(read.operation);
            std::cerr << " payload-bytes=" << read.bytes.size() << '\n';
            return false;
        }
        if (read.bytes.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        bytes.insert(bytes.end(), read.bytes.begin(), read.bytes.end());
    }
    return true;
}

bool transact(
    svm::transport::SerialSession& session,
    const ModbusOracle& oracle,
    svm::transport::SerialSessionGeneration generation,
    const char* scenarioName,
    int transactionIndex,
    bool trace) {
    svm::transport::SerialByteStream& stream = session.byteStream();
    if (trace && transactionIndex == 0) {
        std::cerr << "trace: writing first request scenario=" << scenarioName << '\n';
    }
    const svm::transport::SerialDeadline deadline = deadlineAfter(2000);
    const svm::transport::SerialTerminalResult writeResult = stream.writeBytes(oracle.request, deadline);
    if (!operationBelongsToGeneration(
            writeResult,
            svm::transport::SerialOperationKind::Write,
            generation,
            scenarioName,
            transactionIndex)
        || !writeResult.succeeded()
        || writeResult.byteCount != oracle.request.size()
        || writeResult.error.byteCount != oracle.request.size()) {
        std::cerr << "write failed scenario=" << scenarioName
                  << " transaction=" << transactionIndex;
        printOperationEvidence(writeResult);
        std::cerr << '\n';
        return false;
    }

    std::vector<std::uint8_t> response;
    if (!readExpectedBytes(
            stream,
            oracle.response.size(),
            deadline,
            generation,
            scenarioName,
            transactionIndex,
            response)) {
        return false;
    }
    if (response != oracle.response) {
        std::cerr << "unexpected response scenario=" << scenarioName
                  << " transaction=" << transactionIndex << " expected=";
        printBytes(std::cerr, oracle.response);
        std::cerr << " actual=";
        printBytes(std::cerr, response);
        std::cerr << '\n';
        return false;
    }

    const auto current = session.snapshot();
    if (!current.open() || current.generation != generation) {
        std::cerr << "session generation changed during transaction scenario=" << scenarioName
                  << " transaction=" << transactionIndex
                  << " expected-generation=" << generation
                  << " actual-generation=" << current.generation << '\n';
        return false;
    }
    return true;
}

bool waitForTypedTimeout(
    svm::transport::SerialByteStream& stream,
    svm::transport::SerialSessionGeneration generation,
    int timeoutMs) {
    const svm::transport::SerialDeadline deadline = deadlineAfter(timeoutMs);
    while (true) {
        svm::transport::SerialReadResult read = stream.readAvailable(260, deadline);
        if (!operationBelongsToGeneration(
                read.operation,
                svm::transport::SerialOperationKind::Read,
                generation,
                "timeout",
                0)) {
            return false;
        }
        if (!read.bytes.empty() || read.operation.byteCount != 0 || read.operation.error.byteCount != 0) {
            std::cerr << "unexpected timeout response bytes=";
            printBytes(std::cerr, read.bytes);
            printOperationEvidence(read.operation);
            std::cerr << '\n';
            return false;
        }
        if (read.operation.status == svm::transport::SerialOperationStatus::Timeout) {
            if (read.operation.deadlineStatus != svm::transport::SerialDeadlineStatus::Expired
                || read.operation.error.category != svm::transport::SerialErrorCategory::Timeout) {
                std::cerr << "invalid typed timeout evidence";
                printOperationEvidence(read.operation);
                std::cerr << '\n';
                return false;
            }
            return true;
        }
        if (!read.operation.succeeded()) {
            std::cerr << "unexpected timeout read failure";
            printOperationEvidence(read.operation);
            std::cerr << '\n';
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool waitForActiveWrite(
    svm::transport::SerialWriteScheduler& scheduler,
    svm::transport::SerialOperationId requestId,
    svm::transport::SerialSessionGeneration generation,
    int waitMs,
    const char* scenarioName) {
    const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    do {
        const auto snapshot = scheduler.writeQueueSnapshot();
        if (snapshot.generation != generation) {
            std::cerr << "queue generation changed scenario=" << scenarioName
                      << " expected-generation=" << generation
                      << " actual-generation=" << snapshot.generation << '\n';
            return false;
        }
        if (snapshot.activeCount == 1 && snapshot.activeRequestId == requestId) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < waitDeadline);

    const auto snapshot = scheduler.writeQueueSnapshot();
    std::cerr << "active write not observed scenario=" << scenarioName
              << " request-id=" << requestId
              << " active-id=" << snapshot.activeRequestId
              << " active-count=" << snapshot.activeCount
              << " pending-count=" << snapshot.pendingCount << '\n';
    return false;
}

bool closeSession(
    svm::transport::SerialSession& session,
    svm::transport::SerialSessionGeneration generation,
    const char* scenarioName) {
    const svm::transport::SerialOperationResult result = session.close();
    if (!result.succeeded()
        || result.operation.kind != svm::transport::SerialOperationKind::Close
        || result.operation.generation != generation) {
        std::cerr << "close failed scenario=" << scenarioName;
        printOperationEvidence(result);
        std::cerr << '\n';
        return false;
    }
    const auto closed = session.snapshot();
    if (closed.state != svm::transport::SerialSessionState::Closed
        || closed.generation != svm::transport::kUnassignedSerialSessionGeneration) {
        std::cerr << "session not closed scenario=" << scenarioName
                  << " state=" << static_cast<int>(closed.state)
                  << " generation=" << closed.generation << '\n';
        return false;
    }
    return true;
}

bool validateFreshOpen(
    svm::transport::SerialSession& session,
    const svm::transport::SerialOperationResult& openResult,
    svm::transport::SerialSessionGeneration previousGeneration,
    int reopenIndex,
    const char* scenarioName,
    svm::transport::SerialSessionGeneration& generation) {
    const svm::transport::SerialSessionSnapshot opened = session.snapshot();
    const svm::transport::SerialWriteQueueSnapshot queue = session.writeScheduler().writeQueueSnapshot();
    generation = opened.generation;
    if (!openResult.succeeded()
        || openResult.operation.kind != svm::transport::SerialOperationKind::Open
        || !openResult.operation.assigned()
        || generation == svm::transport::kUnassignedSerialSessionGeneration
        || openResult.operation.generation != generation
        || (previousGeneration != svm::transport::kUnassignedSerialSessionGeneration
            && generation <= previousGeneration)
        || queue.generation != generation
        || !queue.empty()
        || queue.highWaterCount != 0
        || queue.highWaterBytes != 0
        || !session.writeScheduler().takeCompletedWrites().empty()) {
        std::cerr << "fresh open invariant failed scenario=" << scenarioName
                  << " reopen=" << reopenIndex
                  << " previous-generation=" << previousGeneration
                  << " session-generation=" << generation
                  << " queue-generation=" << queue.generation
                  << " queue-count=" << queue.countedCount()
                  << " high-water-count=" << queue.highWaterCount
                  << " high-water-bytes=" << queue.highWaterBytes;
        printOperationEvidence(openResult);
        std::cerr << '\n';
        return false;
    }
    return true;
}

std::vector<std::uint8_t> faultPayload(std::size_t size) {
    const auto& prefix = oracleFor(0).request;
    std::vector<std::uint8_t> payload(size, 0xA5);
    std::copy(prefix.begin(), prefix.end(), payload.begin());
    return payload;
}

bool validateCancelledActiveResult(
    const svm::transport::SerialTerminalResult& result,
    const svm::transport::SerialWriteAdmissionResult& admission,
    const char* scenarioName) {
    if (result.operation.requestId == admission.operation.requestId
        && result.operation.generation == admission.operation.generation
        && result.operation.kind == svm::transport::SerialOperationKind::Write
        && result.status == svm::transport::SerialOperationStatus::Cancelled
        && result.error.category == svm::transport::SerialErrorCategory::SessionClosed) {
        return true;
    }
    std::cerr << "active terminal mismatch scenario=" << scenarioName;
    printOperationEvidence(result);
    std::cerr << " expected-request-id=" << admission.operation.requestId
              << " expected-generation=" << admission.operation.generation << '\n';
    return false;
}

} // namespace

int main() {
    const char* portNameEnv = std::getenv("SVM_NATIVE_SERIAL_LOOPBACK_PORT");
    if (portNameEnv == nullptr || std::string(portNameEnv).empty()) {
        std::cout << "native_win32_serial_loopback_tests skipped: SVM_NATIVE_SERIAL_LOOPBACK_PORT is not set\n";
        return 0;
    }
    const bool trace = std::getenv("SVM_NATIVE_SERIAL_LOOPBACK_TRACE") != nullptr;
    const char* scenarioEnv = std::getenv("SVM_NATIVE_SERIAL_LOOPBACK_SCENARIO");
    const std::string scenario = scenarioEnv == nullptr || std::string(scenarioEnv).empty()
        ? "normal"
        : std::string(scenarioEnv);
    if (scenario != "normal" && scenario != "reopen" && scenario != "timeout"
        && scenario != "cancel" && scenario != "close" && scenario != "stale"
        && scenario != "stress") {
        std::cerr << "SVM_NATIVE_SERIAL_LOOPBACK_SCENARIO invalid: " << scenario
                  << " valid=normal,reopen,timeout,cancel,close,stale,stress\n";
        return 6;
    }

    int iterations = 1;
    int reopenCount = 1;
    int timeoutMs = 100;
    int cancelWaitMs = 250;
    int closeWaitMs = 250;
    int staleWaitMs = 100;
    if (!parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_ITERATIONS", 1, 100000, iterations)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_REOPEN_COUNT", 1, 10000, reopenCount)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_TIMEOUT_MS", 100, 60000, timeoutMs)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_CANCEL_WAIT_MS", 250, kFaultWaitMaxMs, cancelWaitMs)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_CLOSE_WAIT_MS", 250, kFaultWaitMaxMs, closeWaitMs)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_STALE_WAIT_MS", 100, 60000, staleWaitMs)) {
        return 6;
    }

    svm::win32::SerialOpenOptions options;
    options.portName = portNameEnv;
    options.baudRate = 115200;
    options.dataBits = 8;
    options.parity = svm::win32::SerialParity::None;
    options.stopBits = svm::win32::SerialStopBits::One;
    options.flowControl = svm::win32::SerialFlowControl::None;
    options.readTimeoutMs = 100;
    options.writeTimeoutMs = svm::transport::kSerialTerminalResultTargetMs;

    if (trace) {
        std::cerr << "trace: start scenario=" << scenario << " port=" << portNameEnv
                  << " reopen=" << reopenCount << " iterations=" << iterations << '\n';
    }

    if (scenario == "timeout") {
        svm::win32::Win32SerialSession port;
        svm::transport::SerialSession& session = port;
        svm::transport::SerialSessionGeneration generation = 0;
        const auto openResult = session.open(options);
        if (!validateFreshOpen(session, openResult, 0, 0, "timeout", generation)) {
            return 2;
        }
        const auto& request = oracleFor(0).request;
        const auto writeResult = session.byteStream().writeBytes(request, deadlineAfter(options.writeTimeoutMs));
        if (!operationBelongsToGeneration(
                writeResult,
                svm::transport::SerialOperationKind::Write,
                generation,
                "timeout",
                0)
            || !writeResult.succeeded()
            || writeResult.byteCount != request.size()) {
            std::cerr << "timeout request write failed";
            printOperationEvidence(writeResult);
            std::cerr << '\n';
            return 3;
        }
        if (!waitForTypedTimeout(session.byteStream(), generation, timeoutMs)) {
            return 4;
        }
        if (!closeSession(session, generation, "timeout")) {
            return 5;
        }
        std::cout << "native_win32_serial_loopback_tests passed scenario=timeout port=" << portNameEnv
                  << " timeout-ms=" << timeoutMs << " transactions=0 tx=" << request.size() << " rx=0\n";
        return 0;
    }

    if (scenario == "cancel" || scenario == "close") {
        svm::win32::Win32SerialSession port;
        svm::transport::SerialSession& session = port;
        svm::transport::SerialWriteScheduler& scheduler = session.writeScheduler();
        svm::transport::SerialSessionGeneration generation = 0;
        const auto openResult = session.open(options);
        if (!validateFreshOpen(session, openResult, 0, 0, scenario.c_str(), generation)) {
            return 2;
        }

        const std::size_t activePayloadBytes = scenario == "cancel"
            ? svm::transport::kDefaultSerialWriteQueueByteCapacity - cancelMarker().size()
            : svm::transport::kDefaultSerialWriteQueueByteCapacity;
        const auto activeAdmission = scheduler.enqueueWrite(
            faultPayload(activePayloadBytes),
            deadlineAfter(options.writeTimeoutMs));
        if (!activeAdmission.accepted()
            || !activeAdmission.operation.assigned()
            || activeAdmission.operation.generation != generation) {
            std::cerr << "active admission failed scenario=" << scenario;
            printOperationEvidence(activeAdmission);
            std::cerr << '\n';
            return 3;
        }
        const int activeWaitMs = scenario == "cancel" ? cancelWaitMs : closeWaitMs;
        if (!waitForActiveWrite(
                scheduler,
                activeAdmission.operation.requestId,
                generation,
                activeWaitMs,
                scenario.c_str())) {
            return 4;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(activeWaitMs));

        if (scenario == "cancel") {
            const auto pendingAdmission = scheduler.enqueueWrite(
                cancelMarker(),
                deadlineAfter(options.writeTimeoutMs));
            const auto fullSnapshot = scheduler.writeQueueSnapshot();
            if (!pendingAdmission.accepted()
                || !pendingAdmission.operation.assigned()
                || pendingAdmission.operation.generation != generation
                || fullSnapshot.activeRequestId != activeAdmission.operation.requestId
                || fullSnapshot.pendingCount != 1
                || fullSnapshot.countedBytes() != svm::transport::kDefaultSerialWriteQueueByteCapacity) {
                std::cerr << "pending cancellation admission failed";
                printOperationEvidence(pendingAdmission);
                std::cerr << " active-id=" << fullSnapshot.activeRequestId
                          << " pending-count=" << fullSnapshot.pendingCount
                          << " counted-bytes=" << fullSnapshot.countedBytes() << '\n';
                return 4;
            }

            const auto cancelled = scheduler.cancelPendingWrites();
            if (cancelled.size() != 1
                || cancelled.front().operation.requestId != pendingAdmission.operation.requestId
                || cancelled.front().operation.generation != generation
                || cancelled.front().operation.kind != svm::transport::SerialOperationKind::Write
                || cancelled.front().status != svm::transport::SerialOperationStatus::Cancelled
                || cancelled.front().error.category != svm::transport::SerialErrorCategory::Cancelled
                || cancelled.front().byteCount != 0
                || !cancelled.front().terminal()
                || !scheduler.takeCompletedWrites().empty()) {
                std::cerr << "pending cancellation result mismatch count=" << cancelled.size();
                if (!cancelled.empty()) {
                    printOperationEvidence(cancelled.front());
                }
                std::cerr << '\n';
                return 4;
            }
            const auto afterCancel = scheduler.writeQueueSnapshot();
            if (afterCancel.activeRequestId != activeAdmission.operation.requestId
                || afterCancel.activeCount != 1
                || afterCancel.pendingCount != 0
                || afterCancel.countedBytes() != activePayloadBytes) {
                std::cerr << "active write changed by pending cancellation"
                          << " active-id=" << afterCancel.activeRequestId
                          << " active-count=" << afterCancel.activeCount
                          << " pending-count=" << afterCancel.pendingCount
                          << " counted-bytes=" << afterCancel.countedBytes() << '\n';
                return 4;
            }
        }

        const auto closeStarted = std::chrono::steady_clock::now();
        if (!closeSession(session, generation, scenario.c_str())) {
            return 5;
        }
        const auto closeElapsed = std::chrono::steady_clock::now() - closeStarted;
        if (closeElapsed >= std::chrono::milliseconds(
                svm::transport::kSerialTerminalResultTargetMs)) {
            std::cerr << "close exceeded bounded settlement scenario=" << scenario
                      << " elapsed-ms="
                      << std::chrono::duration_cast<std::chrono::milliseconds>(closeElapsed).count() << '\n';
            return 5;
        }

        const auto activeResults = scheduler.takeCompletedWrites();
        if (activeResults.size() != 1
            || !validateCancelledActiveResult(activeResults.front(), activeAdmission, scenario.c_str())
            || !scheduler.takeCompletedWrites().empty()
            || !scheduler.writeQueueSnapshot().empty()) {
            std::cerr << "active close settlement mismatch scenario=" << scenario
                      << " result-count=" << activeResults.size()
                      << " queue-count=" << scheduler.writeQueueSnapshot().countedCount() << '\n';
            return 5;
        }
        const auto postCloseRead = session.byteStream().readAvailable(1, deadlineAfter(timeoutMs));
        if (!postCloseRead.bytes.empty()
            || postCloseRead.operation.status != svm::transport::SerialOperationStatus::RejectedClosed
            || postCloseRead.operation.error.category != svm::transport::SerialErrorCategory::SessionClosed
            || postCloseRead.operation.operation.assigned()
            || postCloseRead.operation.operation.generation
                != svm::transport::kUnassignedSerialSessionGeneration) {
            std::cerr << "post-close read was not rejected scenario=" << scenario;
            printOperationEvidence(postCloseRead.operation);
            std::cerr << '\n';
            return 5;
        }
        std::cout << "native_win32_serial_loopback_tests passed scenario=" << scenario
                  << " port=" << portNameEnv
                  << " active-request-id=" << activeAdmission.operation.requestId
                  << " active-bytes=" << activePayloadBytes
                  << " successful-transactions=0\n";
        return 0;
    }

    if (scenario == "stale") {
        svm::win32::Win32SerialSession port;
        svm::transport::SerialSession& session = port;
        svm::transport::SerialSessionGeneration oldGeneration = 0;
        const auto oldOpen = session.open(options);
        if (!validateFreshOpen(session, oldOpen, 0, 0, "stale", oldGeneration)) {
            return 2;
        }
        if (!transact(session, oracleFor(0), oldGeneration, "stale", 0, trace)) {
            return 3;
        }
        if (!closeSession(session, oldGeneration, "stale")) {
            return 5;
        }
        if (!session.writeScheduler().writeQueueSnapshot().empty()
            || !session.writeScheduler().takeCompletedWrites().empty()) {
            std::cerr << "old generation retained queued work scenario=stale generation="
                      << oldGeneration << '\n';
            return 7;
        }

        const auto staleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(staleWaitMs);
        while (std::chrono::steady_clock::now() < staleDeadline) {
            if (!session.writeScheduler().writeQueueSnapshot().empty()
                || !session.writeScheduler().takeCompletedWrites().empty()) {
                std::cerr << "old generation completion appeared scenario=stale generation="
                          << oldGeneration << '\n';
                return 7;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        svm::transport::SerialSessionGeneration replacementGeneration = 0;
        const auto replacementOpen = session.open(options);
        if (!validateFreshOpen(
                session,
                replacementOpen,
                oldGeneration,
                1,
                "stale",
                replacementGeneration)) {
            return 2;
        }
        if (!transact(session, oracleFor(1), replacementGeneration, "stale", 1, trace)) {
            return 3;
        }
        if (!closeSession(session, replacementGeneration, "stale")
            || !session.writeScheduler().writeQueueSnapshot().empty()
            || !session.writeScheduler().takeCompletedWrites().empty()) {
            return 5;
        }
        std::cout << "native_win32_serial_loopback_tests passed scenario=stale port=" << portNameEnv
                  << " old-generation=" << oldGeneration
                  << " replacement-generation=" << replacementGeneration
                  << " stale-wait-ms=" << staleWaitMs
                  << " transactions=2 tx=16 rx=18\n";
        return 0;
    }

    svm::win32::Win32SerialSession port;
    svm::transport::SerialSession& session = port;
    svm::transport::SerialSessionGeneration previousGeneration = 0;
    long long transmittedBytes = 0;
    long long receivedBytes = 0;
    for (int reopenIndex = 0; reopenIndex < reopenCount; ++reopenIndex) {
        const auto openResult = session.open(options);
        svm::transport::SerialSessionGeneration generation = 0;
        if (!validateFreshOpen(
                session,
                openResult,
                previousGeneration,
                reopenIndex,
                scenario.c_str(),
                generation)) {
            return 2;
        }

        for (int iterationIndex = 0; iterationIndex < iterations; ++iterationIndex) {
            const int transactionIndex = reopenIndex * iterations + iterationIndex;
            const auto& oracle = oracleFor(static_cast<std::size_t>(transactionIndex));
            if (!transact(
                    session,
                    oracle,
                    generation,
                    scenario.c_str(),
                    transactionIndex,
                    trace)) {
                return 3;
            }
            transmittedBytes += static_cast<long long>(oracle.request.size());
            receivedBytes += static_cast<long long>(oracle.response.size());
        }

        if (!closeSession(session, generation, scenario.c_str())
            || !session.writeScheduler().writeQueueSnapshot().empty()
            || !session.writeScheduler().takeCompletedWrites().empty()) {
            return 5;
        }
        previousGeneration = generation;
    }

    const long long transactionCount = static_cast<long long>(iterations) * static_cast<long long>(reopenCount);
    std::cout << "native_win32_serial_loopback_tests passed scenario=" << scenario
              << " port=" << portNameEnv
              << " reopen=" << reopenCount
              << " iterations=" << iterations
              << " transactions=" << transactionCount
              << " tx=" << transmittedBytes
              << " rx=" << receivedBytes << '\n';
    return 0;
}
