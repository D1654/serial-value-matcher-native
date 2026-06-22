#include "win32/native_serial_profile_codec.h"

#include <cassert>
#include <iostream>

namespace {

using svm::win32::SerialFlowControl;
using svm::win32::SerialParity;
using svm::win32::SerialStopBits;

void parityKeysRemainStable() {
    assert(svm::win32::nativeSerialParityKey(SerialParity::None) == "None");
    assert(svm::win32::nativeSerialParityKey(SerialParity::Odd) == "Odd");
    assert(svm::win32::nativeSerialParityKey(SerialParity::Even) == "Even");
    assert(svm::win32::nativeSerialParityKey(SerialParity::Mark) == "Mark");
    assert(svm::win32::nativeSerialParityKey(SerialParity::Space) == "Space");
    assert(svm::win32::nativeSerialParityFromKey("Odd") == SerialParity::Odd);
    assert(svm::win32::nativeSerialParityFromKey("Even") == SerialParity::Even);
    assert(svm::win32::nativeSerialParityFromKey("Mark") == SerialParity::Mark);
    assert(svm::win32::nativeSerialParityFromKey("Space") == SerialParity::Space);
    assert(svm::win32::nativeSerialParityFromKey("unexpected") == SerialParity::None);
}

void stopBitsKeysRemainStable() {
    assert(svm::win32::nativeSerialStopBitsKey(SerialStopBits::One) == "One");
    assert(svm::win32::nativeSerialStopBitsKey(SerialStopBits::OnePointFive) == "OnePointFive");
    assert(svm::win32::nativeSerialStopBitsKey(SerialStopBits::Two) == "Two");
    assert(svm::win32::nativeSerialStopBitsFromKey("OnePointFive") == SerialStopBits::OnePointFive);
    assert(svm::win32::nativeSerialStopBitsFromKey("Two") == SerialStopBits::Two);
    assert(svm::win32::nativeSerialStopBitsFromKey("unexpected") == SerialStopBits::One);
}

void flowControlKeysRemainStable() {
    assert(svm::win32::nativeSerialFlowControlKey(SerialFlowControl::None) == "None");
    assert(svm::win32::nativeSerialFlowControlKey(SerialFlowControl::HardwareRtsCts) == "Hardware");
    assert(svm::win32::nativeSerialFlowControlKey(SerialFlowControl::SoftwareXonXoff) == "Software");
    assert(svm::win32::nativeSerialFlowControlFromKey("Hardware") == SerialFlowControl::HardwareRtsCts);
    assert(svm::win32::nativeSerialFlowControlFromKey("Software") == SerialFlowControl::SoftwareXonXoff);
    assert(svm::win32::nativeSerialFlowControlFromKey("unexpected") == SerialFlowControl::None);
}

} // namespace

int main() {
    parityKeysRemainStable();
    stopBitsKeysRemainStable();
    flowControlKeysRemainStable();

    std::cout << "native_serial_profile_codec_tests passed\n";
    return 0;
}
