#include "win32/win32_serial_types.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void validatesSerialOpenOptions() {
    svm::win32::SerialOpenOptions options;
    options.portName = "COM3";
    assert(svm::win32::validateSerialOpenOptions(options).ok);

    options.portName = "";
    const auto missingPort = svm::win32::validateSerialOpenOptions(options);
    assert(!missingPort.ok);
    assert(contains(missingPort.errorMessage, "未选择串口"));

    options.portName = "ttyUSB0";
    const auto badPort = svm::win32::validateSerialOpenOptions(options);
    assert(!badPort.ok);
    assert(contains(badPort.errorMessage, "COM1"));

    options.portName = R"(\\.\COM12)";
    assert(svm::win32::validateSerialOpenOptions(options).ok);

    options.baudRate = 0;
    const auto badBaud = svm::win32::validateSerialOpenOptions(options);
    assert(!badBaud.ok);
    assert(contains(badBaud.errorMessage, "波特率"));

    options.baudRate = 115200;
    options.dataBits = 9;
    const auto badDataBits = svm::win32::validateSerialOpenOptions(options);
    assert(!badDataBits.ok);
    assert(contains(badDataBits.errorMessage, "数据位"));

    options.dataBits = 8;
    options.readTimeoutMs = -1;
    const auto badTimeout = svm::win32::validateSerialOpenOptions(options);
    assert(!badTimeout.ok);
    assert(contains(badTimeout.errorMessage, "超时"));

    options.readTimeoutMs = 1000;
    options.readBufferSize = 0;
    const auto badBuffer = svm::win32::validateSerialOpenOptions(options);
    assert(!badBuffer.ok);
    assert(contains(badBuffer.errorMessage, "缓冲区"));
}

void normalizesComPortNamesAndDevicePaths() {
    assert(svm::win32::trimPortName("  COM10 \t") == "COM10");
    assert(svm::win32::normalizedComPortName(" com3 ") == "COM3");
    assert(svm::win32::normalizedComPortName(R"(\\.\com42)") == "COM42");
    assert(svm::win32::comPortNumber("COM1") == 1);
    assert(svm::win32::comPortNumber(R"(\\.\COM256)") == 256);
    assert(svm::win32::comPortNumber("COM0") == -1);
    assert(svm::win32::comPortNumber("COM") == -1);

    assert(svm::win32::isLikelyComPortName("COM12"));
    assert(!svm::win32::isLikelyComPortName("ttyS0"));
    assert(svm::win32::isWin32DevicePath(R"(\\.\COM12)"));
    assert(svm::win32::makeWin32DevicePath("COM10") == R"(\\.\COM10)");
    assert(svm::win32::stripWin32DevicePrefix(R"(\\.\COM10)") == "COM10");
}

void keepsChineseNamesForSettings() {
    assert(svm::win32::serialParityName(svm::win32::SerialParity::None) == "无校验");
    assert(svm::win32::serialParityName(svm::win32::SerialParity::Even) == "偶校验");
    assert(svm::win32::serialStopBitsName(svm::win32::SerialStopBits::Two) == "2 位停止位");
    assert(svm::win32::serialFlowControlName(svm::win32::SerialFlowControl::HardwareRtsCts) == "RTS/CTS 硬件流控");
}

void translatesCommonWin32ErrorsToActionableChinese() {
    const std::string missing = svm::win32::win32SerialErrorText(2, "打开串口");
    assert(contains(missing, "打开串口失败"));
    assert(contains(missing, "没有找到"));

    const std::string denied = svm::win32::win32SerialErrorText(5, "打开串口");
    assert(contains(denied, "串口被占用或权限不足"));

    const std::string sharing = svm::win32::win32SerialErrorText(32, "打开串口");
    assert(contains(sharing, "关闭其他串口助手"));

    const std::string timeout = svm::win32::win32SerialErrorText(121, "读取串口");
    assert(contains(timeout, "操作超时"));

    const std::string unknown = svm::win32::win32SerialErrorText(123456, "写入串口");
    assert(contains(unknown, "未知串口错误"));
    assert(contains(unknown, "123456"));
}

} // namespace

int main() {
    validatesSerialOpenOptions();
    normalizesComPortNamesAndDevicePaths();
    keepsChineseNamesForSettings();
    translatesCommonWin32ErrorsToActionableChinese();

    std::cout << "native_win32_serial_tests passed\n";
    return 0;
}
