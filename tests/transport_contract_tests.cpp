#include "transport/serial_transport.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeSerialTransport final : public svm::transport::SerialTransport {
public:
    bool open(svm::transport::SerialOpenOptions options) override {
        options_ = std::move(options);
        open_ = !options_.portName.empty();
        error_.clear();
        return open_;
    }

    void close() override {
        open_ = false;
        queue_.clear();
        completed_.clear();
    }

    bool isOpen() const noexcept override { return open_; }
    std::string endpoint() const override { return options_.portName; }
    std::string lastErrorText() const override { return error_; }
    bool usesHardwareRtsCts() const noexcept override {
        return options_.flowControl == svm::transport::SerialFlowControl::HardwareRtsCts;
    }

    bool setDataTerminalReady(bool enabled) override {
        options_.dataTerminalReady = enabled;
        return open_;
    }

    bool setRequestToSend(bool enabled) override {
        options_.requestToSend = enabled;
        return open_;
    }

    svm::transport::SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload) override {
        if (!open_) {
            error_ = "closed";
            return {false, 0, error_};
        }
        received_ = payload;
        return {true, payload.size(), {}};
    }

    svm::transport::SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) override {
        return queue_.enqueue(std::move(payload), timeoutMs.value_or(svm::transport::kDefaultSerialWriteTimeoutMs));
    }

    std::vector<svm::transport::SerialWriteResult> cancelPendingWrites() override {
        return queue_.cancelAllPending();
    }

    std::vector<svm::transport::SerialWriteResult> takeCompletedWrites() override {
        std::vector<svm::transport::SerialWriteResult> result;
        result.swap(completed_);
        return result;
    }

    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override {
        return queue_.snapshot();
    }

    bool waitForReadyRead(int) override { return !received_.empty(); }

    std::vector<std::uint8_t> readAvailable(std::size_t maxBytes) override {
        const std::size_t count = std::min(maxBytes, received_.size());
        std::vector<std::uint8_t> result(received_.begin(), received_.begin() + static_cast<std::ptrdiff_t>(count));
        received_.erase(received_.begin(), received_.begin() + static_cast<std::ptrdiff_t>(count));
        return result;
    }

private:
    svm::transport::SerialOpenOptions options_;
    svm::transport::SerialWriteQueue queue_;
    std::vector<svm::transport::SerialWriteResult> completed_;
    std::vector<std::uint8_t> received_;
    std::string error_;
    bool open_ = false;
};

} // namespace

int main() {
    FakeSerialTransport transport;
    svm::transport::SerialOpenOptions options;
    options.portName = "COM7";
    options.flowControl = svm::transport::SerialFlowControl::HardwareRtsCts;

    assert(transport.open(options));
    assert(transport.isOpen());
    assert(transport.endpoint() == "COM7");
    assert(transport.usesHardwareRtsCts());

    const std::vector<std::uint8_t> payload{1, 2, 3};
    const auto write = transport.writeBytes(payload);
    assert(write.ok && write.byteCount == payload.size());
    assert(transport.waitForReadyRead(0));
    assert(transport.readAvailable(2) == std::vector<std::uint8_t>({1, 2}));
    assert(transport.readAvailable(2) == std::vector<std::uint8_t>({3}));

    const auto queued = transport.enqueueWrite(payload);
    assert(queued.accepted());
    assert(transport.writeQueueSnapshot().pendingCount == 1);
    const auto cancelled = transport.cancelPendingWrites();
    assert(cancelled.size() == 1);
    assert(cancelled.front().status == svm::transport::SerialWriteResultStatus::Cancelled);
    assert(transport.writeQueueSnapshot().empty());

    transport.close();
    assert(!transport.isOpen());
    return 0;
}
