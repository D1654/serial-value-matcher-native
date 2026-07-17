#include "win32/win32_serial_session.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

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
    std::cerr << " status=" << static_cast<int>(result.status)
              << " category=" << static_cast<int>(result.error.category)
              << " nativeCode=" << result.error.nativeCode
              << " bytes=" << result.byteCount;
}

bool readExpectedBytes(
    svm::transport::SerialByteStream& stream,
    std::size_t byteCount,
    const svm::transport::SerialDeadline& deadline,
    int reopenIndex,
    int iterationIndex,
    std::vector<std::uint8_t>& bytes) {
    while (bytes.size() < byteCount) {
        if (deadline.expiresAt.has_value()
            && std::chrono::steady_clock::now() >= *deadline.expiresAt) {
            break;
        }

        svm::transport::SerialReadResult read = stream.readAvailable(byteCount - bytes.size(), deadline);
        if (read.operation.status == svm::transport::SerialOperationStatus::Timeout) {
            break;
        }
        if (!read.operation.succeeded()) {
            std::cerr << "read failed reopen=" << reopenIndex << " iteration=" << iterationIndex;
            printOperationEvidence(read.operation);
            std::cerr << '\n';
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
    svm::transport::SerialByteStream& stream,
    const std::vector<std::uint8_t>& request,
    const std::vector<std::uint8_t>& expected,
    int reopenIndex,
    int iterationIndex,
    bool trace) {
    if (trace && iterationIndex == 0) {
        std::cerr << "trace: writing first request reopen=" << reopenIndex << '\n';
    }
    const svm::transport::SerialDeadline deadline = deadlineAfter(2000);
    const svm::transport::SerialTerminalResult writeResult = stream.writeBytes(request, deadline);
    if (!writeResult.succeeded() || writeResult.byteCount != request.size()) {
        std::cerr << "write failed reopen=" << reopenIndex << " iteration=" << iterationIndex;
        printOperationEvidence(writeResult);
        std::cerr << '\n';
        return false;
    }

    if (trace && iterationIndex == 0) {
        std::cerr << "trace: waiting first response reopen=" << reopenIndex << '\n';
    }
    std::vector<std::uint8_t> response;
    if (!readExpectedBytes(stream, expected.size(), deadline, reopenIndex, iterationIndex, response)) {
        return false;
    }
    if (response != expected) {
        std::cerr << "unexpected response reopen=" << reopenIndex << " iteration=" << iterationIndex
                  << " expected=";
        printBytes(std::cerr, expected);
        std::cerr << " actual=";
        printBytes(std::cerr, response);
        std::cerr << '\n';
        return false;
    }

    return true;
}

bool waitForNoData(
    svm::transport::SerialByteStream& stream,
    int timeoutMs,
    const char* scenarioName) {
    const svm::transport::SerialDeadline deadline = deadlineAfter(timeoutMs);
    while (!deadline.expiresAt.has_value()
           || std::chrono::steady_clock::now() < *deadline.expiresAt) {
        svm::transport::SerialReadResult read = stream.readAvailable(260, deadline);
        if (read.operation.status == svm::transport::SerialOperationStatus::Timeout) {
            return true;
        }
        if (!read.operation.succeeded()) {
            std::cerr << "unexpected read error scenario=" << scenarioName;
            printOperationEvidence(read.operation);
            std::cerr << '\n';
            return false;
        }
        if (!read.bytes.empty()) {
            std::cerr << "unexpected ready-read scenario=" << scenarioName << " bytes=";
            printBytes(std::cerr, read.bytes);
            std::cerr << '\n';
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

bool closeSession(svm::transport::SerialSession& session, const char* scenarioName) {
    const svm::transport::SerialOperationResult result = session.close();
    if (result.succeeded()) {
        return true;
    }
    std::cerr << "close failed scenario=" << scenarioName;
    printOperationEvidence(result);
    std::cerr << '\n';
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
        && scenario != "cancel" && scenario != "stress") {
        std::cerr << "SVM_NATIVE_SERIAL_LOOPBACK_SCENARIO invalid: " << scenario
                  << " valid=normal,reopen,timeout,cancel,stress\n";
        return 6;
    }

    int iterations = 1;
    int reopenCount = 1;
    int timeoutMs = 100;
    int cancelWaitMs = 150;
    if (!parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_ITERATIONS", 1, 100000, iterations)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_REOPEN_COUNT", 1, 10000, reopenCount)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_TIMEOUT_MS", 100, 60000, timeoutMs)
        || !parsePositiveIntEnv("SVM_NATIVE_SERIAL_LOOPBACK_CANCEL_WAIT_MS", 150, 60000, cancelWaitMs)) {
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
    options.writeTimeoutMs = 1000;

    const std::vector<std::uint8_t> request = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    const std::vector<std::uint8_t> expected = {0x01, 0x03, 0x04, 0x41, 0x48, 0x00, 0x00, 0x7B, 0xF3};
    if (trace) {
        std::cerr << "trace: start scenario=" << scenario << " port=" << portNameEnv << " reopen=" << reopenCount
                  << " iterations=" << iterations << '\n';
    }

    if (scenario == "timeout") {
        svm::win32::Win32SerialSession port;
        svm::transport::SerialSession& session = port;
        svm::transport::SerialByteStream& stream = session.byteStream();
        const svm::transport::SerialOperationResult openResult = session.open(options);
        if (!openResult.succeeded()) {
            std::cerr << "open failed scenario=timeout";
            printOperationEvidence(openResult);
            std::cerr << '\n';
            return 2;
        }
        if (!waitForNoData(stream, timeoutMs, "timeout")) {
            return 4;
        }
        if (!closeSession(session, "timeout")) {
            return 5;
        }
        std::cout << "native_win32_serial_loopback_tests passed scenario=timeout port=" << portNameEnv
                  << " timeout-ms=" << timeoutMs << " transactions=0 tx=0 rx=0\n";
        return 0;
    }

    if (scenario == "cancel") {
        svm::win32::Win32SerialSession port;
        svm::transport::SerialSession& session = port;
        svm::transport::SerialByteStream& stream = session.byteStream();
        const svm::transport::SerialOperationResult openResult = session.open(options);
        if (!openResult.succeeded()) {
            std::cerr << "open failed scenario=cancel";
            printOperationEvidence(openResult);
            std::cerr << '\n';
            return 2;
        }
        const svm::transport::SerialTerminalResult writeResult =
            stream.writeBytes(request, deadlineAfter(options.writeTimeoutMs));
        if (!writeResult.succeeded() || writeResult.byteCount != request.size()) {
            std::cerr << "cancel write failed";
            printOperationEvidence(writeResult);
            std::cerr << '\n';
            return 3;
        }
        if (!waitForNoData(stream, cancelWaitMs, "cancel")) {
            return 4;
        }
        if (!closeSession(session, "cancel")) {
            return 5;
        }
        std::cout << "native_win32_serial_loopback_tests passed scenario=cancel port=" << portNameEnv
                  << " cancel-wait-ms=" << cancelWaitMs
                  << " transactions=1 tx=" << request.size() << " rx=0\n";
        return 0;
    }

    svm::win32::Win32SerialSession port;
    svm::transport::SerialSession& session = port;
    svm::transport::SerialByteStream& stream = session.byteStream();
    for (int reopenIndex = 0; reopenIndex < reopenCount; ++reopenIndex) {
        if (trace) {
            std::cerr << "trace: opening reopen=" << reopenIndex << '\n';
        }
        const svm::transport::SerialOperationResult openResult = session.open(options);
        if (!openResult.succeeded()) {
            std::cerr << "open failed reopen=" << reopenIndex;
            printOperationEvidence(openResult);
            std::cerr << '\n';
            return 2;
        }
        if (trace) {
            std::cerr << "trace: opened reopen=" << reopenIndex << '\n';
        }
        const svm::transport::SerialSessionSnapshot openedSession = session.snapshot();
        const svm::transport::SerialWriteQueueSnapshot openedQueue =
            session.writeScheduler().writeQueueSnapshot();
        if (openedSession.generation == svm::transport::kUnassignedSerialSessionGeneration
            || openedSession.generation != openResult.operation.generation
            || openedQueue.generation != openedSession.generation
            || openedQueue.highWaterCount != 0
            || openedQueue.highWaterBytes != 0) {
            std::cerr << "queue generation reset failed reopen=" << reopenIndex
                      << " session-generation=" << openedSession.generation
                      << " queue-generation=" << openedQueue.generation
                      << " high-water-count=" << openedQueue.highWaterCount
                      << " high-water-bytes=" << openedQueue.highWaterBytes << '\n';
            return 7;
        }

        for (int iterationIndex = 0; iterationIndex < iterations; ++iterationIndex) {
            if (!transact(stream, request, expected, reopenIndex, iterationIndex, trace)) {
                return 3;
            }
        }

        if (!closeSession(session, scenario.c_str())) {
            return 5;
        }
    }

    const long long transactionCount = static_cast<long long>(iterations) * static_cast<long long>(reopenCount);
    std::cout << "native_win32_serial_loopback_tests passed scenario=" << scenario
              << " port=" << portNameEnv
              << " reopen=" << reopenCount
              << " iterations=" << iterations
              << " transactions=" << transactionCount
              << " tx=" << transactionCount * static_cast<long long>(request.size())
              << " rx=" << transactionCount * static_cast<long long>(expected.size()) << '\n';
    return 0;
}
