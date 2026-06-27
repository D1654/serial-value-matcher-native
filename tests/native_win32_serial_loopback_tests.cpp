#include "win32/win32_serial_port.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printBytes(const std::vector<std::uint8_t>& bytes) {
    for (std::uint8_t byte : bytes) {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::cout << digits[(byte >> 4) & 0x0F] << digits[byte & 0x0F] << ' ';
    }
}

} // namespace

int main() {
    const char* portNameEnv = std::getenv("SVM_NATIVE_SERIAL_LOOPBACK_PORT");
    if (portNameEnv == nullptr || std::string(portNameEnv).empty()) {
        std::cout << "native_win32_serial_loopback_tests skipped: SVM_NATIVE_SERIAL_LOOPBACK_PORT is not set\n";
        return 0;
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

    svm::win32::Win32SerialPort port;
    if (!port.open(options)) {
        std::cerr << "open failed: " << port.lastErrorText() << '\n';
        return 2;
    }

    const std::vector<std::uint8_t> request = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    const std::vector<std::uint8_t> expected = {0x01, 0x03, 0x04, 0x41, 0x48, 0x00, 0x00, 0x7B, 0xF3};
    const auto writeResult = port.writeBytes(request);
    if (!writeResult.ok || writeResult.byteCount != request.size()) {
        std::cerr << "write failed: " << writeResult.errorMessage << " bytes=" << writeResult.byteCount << '\n';
        return 3;
    }

    if (!port.waitForReadyRead(2000)) {
        std::cerr << "wait failed: " << port.lastErrorText() << '\n';
        return 4;
    }

    const std::vector<std::uint8_t> response = port.readAvailable(64);
    if (response != expected) {
        std::cerr << "unexpected response: ";
        printBytes(response);
        std::cerr << '\n';
        return 5;
    }

    port.close();
    std::cout << "native_win32_serial_loopback_tests passed port=" << portNameEnv
              << " tx=" << request.size() << " rx=" << response.size() << '\n';
    return 0;
}
