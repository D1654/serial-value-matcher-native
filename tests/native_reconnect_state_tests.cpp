#include "win32/native_reconnect_state.h"

#include <cassert>
#include <iostream>
#include <optional>
#include <type_traits>
#include <utility>

namespace {

using ReconnectOptionsResult = decltype(
    std::declval<const svm::win32::NativeReconnectState&>().reconnectOptions());

template <typename Options>
concept ContainsRequestId = requires(const Options& options) {
    options.requestId;
};

template <typename Options>
concept ContainsPayload = requires(const Options& options) {
    options.payload;
};

static_assert(std::is_same_v<
              ReconnectOptionsResult,
              std::optional<svm::win32::SerialOpenOptions>>);
static_assert(!ContainsRequestId<svm::win32::SerialOpenOptions>);
static_assert(!ContainsPayload<svm::win32::SerialOpenOptions>);

svm::win32::SerialOpenOptions optionsFor(std::string portName) {
    svm::win32::SerialOpenOptions options;
    options.portName = std::move(portName);
    options.baudRate = 57600;
    options.dataTerminalReady = false;
    options.requestToSend = true;
    return options;
}

void successfulOpenStoresOptionsAndClearsWaiting() {
    svm::win32::NativeReconnectState state;
    state.startWaiting("COM7");
    assert(state.waiting());

    const auto options = optionsFor("COM3");
    state.rememberSuccessfulOpen(options);
    assert(!state.waiting());
    assert(state.hasLastOpenOptions());
    assert(state.lastOpenOptions()->portName == "COM3");
    assert(state.lastOpenOptions()->baudRate == 57600);
}

void waitingRequiresLastOptionsAndClosedSerial() {
    svm::win32::NativeReconnectState state;
    state.startWaiting("COM3");
    assert(!state.shouldTryReconnect(false));
    assert(!state.reconnectOptions().has_value());

    state.rememberSuccessfulOpen(optionsFor("COM3"));
    state.startWaiting("COM4");
    assert(!state.shouldTryReconnect(true));
    assert(state.shouldTryReconnect(false));

    const auto reconnect = state.reconnectOptions();
    assert(reconnect.has_value());
    assert(reconnect->portName == "COM4");
    assert(reconnect->baudRate == 57600);
}

void reconnectCopiesTheCompleteConfigurationAndOnlyReplacesEndpoint() {
    svm::win32::SerialOpenOptions original;
    original.portName = "COM5";
    original.baudRate = 230400;
    original.dataBits = 7;
    original.parity = svm::win32::SerialParity::Odd;
    original.stopBits = svm::win32::SerialStopBits::Two;
    original.flowControl = svm::win32::SerialFlowControl::SoftwareXonXoff;
    original.dataTerminalReady = true;
    original.requestToSend = false;
    original.readTimeoutMs = 125;
    original.writeTimeoutMs = 250;
    original.readBufferSize = 8192;

    svm::win32::NativeReconnectState state;
    state.rememberSuccessfulOpen(original);
    state.startWaiting("COM17");

    const auto reconnect = state.reconnectOptions();
    assert(reconnect.has_value());
    assert(reconnect->portName == "COM17");
    assert(reconnect->baudRate == original.baudRate);
    assert(reconnect->dataBits == original.dataBits);
    assert(reconnect->parity == original.parity);
    assert(reconnect->stopBits == original.stopBits);
    assert(reconnect->flowControl == original.flowControl);
    assert(reconnect->dataTerminalReady == original.dataTerminalReady);
    assert(reconnect->requestToSend == original.requestToSend);
    assert(reconnect->readTimeoutMs == original.readTimeoutMs);
    assert(reconnect->writeTimeoutMs == original.writeTimeoutMs);
    assert(reconnect->readBufferSize == original.readBufferSize);

    assert(state.lastOpenOptions()->portName == "COM5");
    assert(state.reconnectPortName() == "COM17");
}

void reconnectTerminalStateStopsWaiting() {
    svm::win32::NativeReconnectState state;
    state.rememberSuccessfulOpen(optionsFor("COM3"));
    state.startWaiting("COM3");
    state.markReconnectFailed();
    assert(!state.waiting());

    state.startWaiting("COM3");
    state.markReconnectSucceeded();
    assert(!state.waiting());
}

void lineControlUpdatesLastOpenOptionsOnlyWhenPresent() {
    svm::win32::NativeReconnectState state;
    assert(!state.updateDataTerminalReady(true));
    assert(!state.updateRequestToSend(false));

    state.rememberSuccessfulOpen(optionsFor("COM3"));
    assert(state.updateDataTerminalReady(true));
    assert(state.updateRequestToSend(false));
    assert(state.lastOpenOptions()->dataTerminalReady);
    assert(!state.lastOpenOptions()->requestToSend);
}

} // namespace

int main() {
    successfulOpenStoresOptionsAndClearsWaiting();
    waitingRequiresLastOptionsAndClosedSerial();
    reconnectCopiesTheCompleteConfigurationAndOnlyReplacesEndpoint();
    reconnectTerminalStateStopsWaiting();
    lineControlUpdatesLastOpenOptionsOnlyWhenPresent();

    std::cout << "native_reconnect_state_tests passed\n";
    return 0;
}
