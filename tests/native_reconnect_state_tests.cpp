#include "win32/native_reconnect_state.h"

#include <cassert>
#include <iostream>

namespace {

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
    reconnectTerminalStateStopsWaiting();
    lineControlUpdatesLastOpenOptionsOnlyWhenPresent();

    std::cout << "native_reconnect_state_tests passed\n";
    return 0;
}
