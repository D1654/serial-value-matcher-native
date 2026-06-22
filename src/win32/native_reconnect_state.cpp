#include "win32/native_reconnect_state.h"

#include <utility>

namespace svm::win32 {

void NativeReconnectState::rememberSuccessfulOpen(const SerialOpenOptions& options) {
    lastOpenOptions_ = options;
    clearWaiting();
}

void NativeReconnectState::clearWaiting() noexcept {
    reconnectPortName_.clear();
    waiting_ = false;
}

void NativeReconnectState::startWaiting(std::string portName) {
    reconnectPortName_ = std::move(portName);
    waiting_ = true;
}

bool NativeReconnectState::waiting() const noexcept {
    return waiting_;
}

const std::string& NativeReconnectState::reconnectPortName() const noexcept {
    return reconnectPortName_;
}

bool NativeReconnectState::hasLastOpenOptions() const noexcept {
    return lastOpenOptions_.has_value();
}

const std::optional<SerialOpenOptions>& NativeReconnectState::lastOpenOptions() const noexcept {
    return lastOpenOptions_;
}

bool NativeReconnectState::shouldTryReconnect(bool serialOpen) const noexcept {
    return waiting_ && lastOpenOptions_.has_value() && !serialOpen;
}

std::optional<SerialOpenOptions> NativeReconnectState::reconnectOptions() const {
    if (!waiting_ || !lastOpenOptions_.has_value()) {
        return std::nullopt;
    }
    SerialOpenOptions options = *lastOpenOptions_;
    options.portName = reconnectPortName_;
    return options;
}

void NativeReconnectState::markReconnectFailed() noexcept {
    waiting_ = false;
}

void NativeReconnectState::markReconnectSucceeded() noexcept {
    waiting_ = false;
}

bool NativeReconnectState::updateDataTerminalReady(bool enabled) noexcept {
    if (!lastOpenOptions_.has_value()) {
        return false;
    }
    lastOpenOptions_->dataTerminalReady = enabled;
    return true;
}

bool NativeReconnectState::updateRequestToSend(bool enabled) noexcept {
    if (!lastOpenOptions_.has_value()) {
        return false;
    }
    lastOpenOptions_->requestToSend = enabled;
    return true;
}

} // namespace svm::win32
