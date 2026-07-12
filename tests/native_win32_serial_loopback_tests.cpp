#include "win32/win32_serial_port.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
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

std::vector<std::uint8_t> readExpectedBytes(
    svm::transport::SerialTransport& port,
    std::size_t byteCount,
    int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::vector<std::uint8_t> bytes;
    while (bytes.size() < byteCount) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            break;
        }

        const int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        const int pollMs = std::clamp(remainingMs, 1, 100);
        if (!port.waitForReadyRead(pollMs)) {
            if (!port.lastErrorText().empty()) {
                break;
            }
            continue;
        }

        std::vector<std::uint8_t> chunk = port.readAvailable(byteCount - bytes.size());
        if (chunk.empty()) {
            if (!port.lastErrorText().empty()) {
                break;
            }
            continue;
        }
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    }
    return bytes;
}

bool transact(
    svm::transport::SerialTransport& port,
    const std::vector<std::uint8_t>& request,
    const std::vector<std::uint8_t>& expected,
    int reopenIndex,
    int iterationIndex,
    bool trace) {
    if (trace && iterationIndex == 0) {
        std::cerr << "trace: writing first request reopen=" << reopenIndex << '\n';
    }
    const auto writeResult = port.writeBytes(request);
    if (!writeResult.ok || writeResult.byteCount != request.size()) {
        std::cerr << "write failed reopen=" << reopenIndex << " iteration=" << iterationIndex
                  << " error=" << writeResult.errorMessage << " bytes=" << writeResult.byteCount << '\n';
        return false;
    }

    if (trace && iterationIndex == 0) {
        std::cerr << "trace: waiting first response reopen=" << reopenIndex << '\n';
    }
    const std::vector<std::uint8_t> response = readExpectedBytes(port, expected.size(), 2000);
    if (response != expected) {
        std::cerr << "unexpected response reopen=" << reopenIndex << " iteration=" << iterationIndex
                  << " expected=";
        printBytes(std::cerr, expected);
        std::cerr << " actual=";
        printBytes(std::cerr, response);
        std::cerr << " lastError=" << port.lastErrorText() << '\n';
        return false;
    }

    return true;
}

bool waitForNoData(svm::transport::SerialTransport& port, int timeoutMs, const char* scenarioName) {
    if (port.waitForReadyRead(timeoutMs)) {
        const std::vector<std::uint8_t> unexpected = port.readAvailable(260);
        std::cerr << "unexpected ready-read scenario=" << scenarioName << " bytes=";
        printBytes(std::cerr, unexpected);
        std::cerr << '\n';
        return false;
    }
    if (!port.lastErrorText().empty()) {
        std::cerr << "unexpected wait error scenario=" << scenarioName
                  << " error=" << port.lastErrorText() << '\n';
        return false;
    }
    return true;
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
        svm::win32::Win32SerialPort port;
        if (!port.open(options)) {
            std::cerr << "open failed scenario=timeout: " << port.lastErrorText() << '\n';
            return 2;
        }
        if (!waitForNoData(port, timeoutMs, "timeout")) {
            return 4;
        }
        port.close();
        std::cout << "native_win32_serial_loopback_tests passed scenario=timeout port=" << portNameEnv
                  << " timeout-ms=" << timeoutMs << " transactions=0 tx=0 rx=0\n";
        return 0;
    }

    if (scenario == "cancel") {
        svm::win32::Win32SerialPort port;
        if (!port.open(options)) {
            std::cerr << "open failed scenario=cancel: " << port.lastErrorText() << '\n';
            return 2;
        }
        const auto writeResult = port.writeBytes(request);
        if (!writeResult.ok || writeResult.byteCount != request.size()) {
            std::cerr << "cancel write failed error=" << writeResult.errorMessage
                      << " bytes=" << writeResult.byteCount << '\n';
            return 3;
        }
        if (!waitForNoData(port, cancelWaitMs, "cancel")) {
            return 4;
        }
        port.close();
        std::cout << "native_win32_serial_loopback_tests passed scenario=cancel port=" << portNameEnv
                  << " cancel-wait-ms=" << cancelWaitMs
                  << " transactions=1 tx=" << request.size() << " rx=0\n";
        return 0;
    }

    for (int reopenIndex = 0; reopenIndex < reopenCount; ++reopenIndex) {
        svm::win32::Win32SerialPort port;
        if (trace) {
            std::cerr << "trace: opening reopen=" << reopenIndex << '\n';
        }
        if (!port.open(options)) {
            std::cerr << "open failed reopen=" << reopenIndex << ": " << port.lastErrorText() << '\n';
            return 2;
        }
        if (trace) {
            std::cerr << "trace: opened reopen=" << reopenIndex << '\n';
        }

        for (int iterationIndex = 0; iterationIndex < iterations; ++iterationIndex) {
            if (!transact(port, request, expected, reopenIndex, iterationIndex, trace)) {
                return 3;
            }
        }

        port.close();
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
