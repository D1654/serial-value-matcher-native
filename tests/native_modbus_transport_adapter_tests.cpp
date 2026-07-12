#include "transport/serial_rtu_transport.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeSerialTransport final : public svm::transport::SerialTransport {
public:
    bool open(svm::transport::SerialOpenOptions options) override {
        options_ = std::move(options);
        open_ = true;
        return true;
    }

    void close() override { open_ = false; }
    bool isOpen() const noexcept override { return open_; }
    std::string endpoint() const override { return "COM9"; }
    std::string lastErrorText() const override { return error_; }
    bool usesHardwareRtsCts() const noexcept override { return false; }
    bool setDataTerminalReady(bool) override { return open_; }
    bool setRequestToSend(bool) override { return open_; }

    svm::transport::SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload) override {
        writes.push_back(payload);
        if (writeFails) {
            error_ = "write failed";
            return {false, 0, error_};
        }
        return {true, partialWrite ? payload.size() - 1 : payload.size(), {}};
    }

    svm::transport::SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) override {
        return queue_.enqueue(std::move(payload), timeoutMs.value_or(svm::transport::kDefaultSerialWriteTimeoutMs));
    }

    std::vector<svm::transport::SerialWriteResult> cancelPendingWrites() override {
        return queue_.cancelAllPending();
    }

    std::vector<svm::transport::SerialWriteResult> takeCompletedWrites() override { return {}; }
    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override { return queue_.snapshot(); }

    bool waitForReadyRead(int) override {
        return !chunks.empty();
    }

    std::vector<std::uint8_t> readAvailable(std::size_t) override {
        if (chunks.empty()) {
            return {};
        }
        auto chunk = std::move(chunks.front());
        chunks.pop_front();
        return chunk;
    }

    bool writeFails = false;
    bool partialWrite = false;
    std::deque<std::vector<std::uint8_t>> chunks;
    std::vector<std::vector<std::uint8_t>> writes;

private:
    svm::transport::SerialOpenOptions options_;
    svm::transport::SerialWriteQueue queue_;
    std::string error_;
    bool open_ = true;
};

svm::core::ByteBuffer request() {
    return {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
}

void succeedsWithChunkedResponseAndOrderedFrames() {
    FakeSerialTransport serial;
    serial.chunks.push_back({0x01, 0x03, 0x02});
    serial.chunks.push_back({0x12, 0x34, 0x00, 0x00});
    std::vector<bool> frameDirections;

    svm::transport::SerialRtuTransport transport(
        serial,
        {
            .nowUtc = [] { return std::string("2026-07-11T00:00:00Z"); },
            .onFrame = [&frameDirections](bool tx, const svm::core::ByteBuffer&) {
                frameDirections.push_back(tx);
            },
        });

    const auto exchange = transport.exchange(request(), 100);
    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(exchange.endpoint == "COM9");
    assert(exchange.responseFrame.size() == 7);
    assert(serial.writes.size() == 1);
    assert(frameDirections == std::vector<bool>({true, false}));
    assert(!transport.serialFailed());
}

void reportsWriteFailureAndPartialWrite() {
    FakeSerialTransport serial;
    serial.writeFails = true;
    svm::transport::SerialRtuTransport transport(serial);
    const auto failed = transport.exchange(request(), 100);
    assert(failed.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(failed.errorMessage == "write failed");
    assert(transport.serialFailed());

    FakeSerialTransport partialSerial;
    partialSerial.partialWrite = true;
    svm::transport::SerialRtuTransport partialTransport(partialSerial);
    const auto partial = partialTransport.exchange(request(), 100);
    assert(partial.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(partial.errorMessage == "Modbus 请求发送不完整。");
}

void reportsCancellationAndTimeout() {
    FakeSerialTransport serial;
    svm::transport::SerialRtuTransport cancelled(
        serial,
        {.shouldCancel = [] { return true; }});
    const auto cancelledExchange = cancelled.exchange(request(), 100);
    assert(cancelledExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(cancelled.cancelObserved());

    FakeSerialTransport timeoutSerial;
    svm::transport::SerialRtuTransport timeout(
        timeoutSerial,
        {.timeoutErrorMessage = "custom timeout"});
    const auto timeoutExchange = timeout.exchange(request(), 1);
    assert(timeoutExchange.status == svm::core::modbus::RtuTransportExchangeStatus::Timeout);
    assert(timeoutExchange.errorMessage == "custom timeout");
    assert(!timeout.serialFailed());
}

} // namespace

int main() {
    succeedsWithChunkedResponseAndOrderedFrames();
    reportsWriteFailureAndPartialWrite();
    reportsCancellationAndTimeout();
    return 0;
}
