#pragma once

#include "win32/win32_serial_types.h"

#include <optional>
#include <string>

namespace svm::win32 {

class NativeReconnectState final {
public:
    void rememberSuccessfulOpen(const SerialOpenOptions& options);
    void clearWaiting() noexcept;
    void startWaiting(std::string portName);

    bool waiting() const noexcept;
    const std::string& reconnectPortName() const noexcept;
    bool hasLastOpenOptions() const noexcept;
    const std::optional<SerialOpenOptions>& lastOpenOptions() const noexcept;

    bool shouldTryReconnect(bool serialOpen) const noexcept;
    std::optional<SerialOpenOptions> reconnectOptions() const;
    void markReconnectFailed() noexcept;
    void markReconnectSucceeded() noexcept;

    bool updateDataTerminalReady(bool enabled) noexcept;
    bool updateRequestToSend(bool enabled) noexcept;

private:
    std::optional<SerialOpenOptions> lastOpenOptions_;
    std::string reconnectPortName_;
    bool waiting_ = false;
};

} // namespace svm::win32
