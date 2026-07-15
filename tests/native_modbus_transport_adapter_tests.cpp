#include "transport/serial_rtu_transport.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr svm::transport::SerialSessionGeneration kGeneration = 7;
constexpr const char* kEndpoint = "COM9";

svm::transport::SerialTerminalResult terminalResult(
    svm::transport::SerialOperationKind kind,
    svm::transport::SerialOperationStatus status,
    svm::transport::SerialSessionGeneration generation,
    std::size_t byteCount = 0,
    svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None,
    std::uint32_t nativeCode = 0) {
    return {
        .operation = {
            .requestId = 1,
            .generation = generation,
            .kind = kind,
        },
        .status = status,
        .deadlineStatus = status == svm::transport::SerialOperationStatus::Timeout
            ? svm::transport::SerialDeadlineStatus::Expired
            : svm::transport::SerialDeadlineStatus::Met,
        .byteCount = byteCount,
        .endpoint = kEndpoint,
        .error = {
            .category = category,
            .nativeCode = nativeCode,
            .byteCount = byteCount,
        },
    };
}

svm::transport::SerialReadResult readResult(
    svm::transport::SerialOperationStatus status,
    svm::transport::SerialSessionGeneration generation,
    std::vector<std::uint8_t> bytes = {},
    svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None) {
    const std::size_t byteCount = bytes.size();
    return {
        .operation = terminalResult(
            svm::transport::SerialOperationKind::Read,
            status,
            generation,
            byteCount,
            category),
        .bytes = std::move(bytes),
    };
}

class FakeSerialByteStream final : public svm::transport::SerialByteStream {
public:
    svm::transport::SerialTerminalResult writeBytes(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline = {}) override {
        writes.push_back(std::move(payload));
        writeDeadlines.push_back(deadline);
        assert(!writeResults.empty());
        auto result = std::move(writeResults.front());
        writeResults.pop_front();
        result.operation.deadline = deadline;
        return result;
    }

    svm::transport::SerialReadResult readAvailable(
        std::size_t maxBytes,
        svm::transport::SerialDeadline deadline = {}) override {
        readLimits.push_back(maxBytes);
        readDeadlines.push_back(deadline);
        if (onRead) {
            onRead();
        }
        assert(!readResults.empty());
        auto result = std::move(readResults.front());
        readResults.pop_front();
        result.operation.operation.deadline = deadline;
        return result;
    }

    std::deque<svm::transport::SerialTerminalResult> writeResults;
    std::deque<svm::transport::SerialReadResult> readResults;
    std::vector<std::vector<std::uint8_t>> writes;
    std::vector<svm::transport::SerialDeadline> writeDeadlines;
    std::vector<std::size_t> readLimits;
    std::vector<svm::transport::SerialDeadline> readDeadlines;
    std::function<void()> onRead;
};

svm::transport::SerialRtuTransportOptions transportOptions(
    svm::transport::SerialSessionGeneration& currentGeneration) {
    return {
        .generation = kGeneration,
        .endpoint = kEndpoint,
        .generationIsCurrent = [&currentGeneration](svm::transport::SerialSessionGeneration expected) {
            return currentGeneration == expected;
        },
    };
}

svm::core::ByteBuffer request() {
    return {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
}

svm::core::ByteBuffer response() {
    const svm::core::ByteBuffer body{0x01, 0x03, 0x02, 0x12, 0x34};
    return svm::core::modbus::appendCrc16Modbus(body);
}

void succeedsWithTypedChunkedResponseAndSharedDeadline() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    const auto expectedResponse = response();
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {expectedResponse.begin(), expectedResponse.begin() + 3}));
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {expectedResponse.begin() + 3, expectedResponse.end()}));

    struct Frame {
        bool tx = false;
        svm::core::ByteBuffer bytes;
    };
    std::vector<Frame> frames;
    auto currentGeneration = kGeneration;
    auto options = transportOptions(currentGeneration);
    options.nowUtc = [] { return std::string("2026-07-11T00:00:00Z"); };
    options.onFrame = [&frames](bool tx, const svm::core::ByteBuffer& bytes) {
        frames.push_back({tx, bytes});
    };
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(exchange.endpoint == kEndpoint);
    assert(exchange.responseFrame == expectedResponse);
    assert(serial.writes == std::vector<svm::core::ByteBuffer>({request()}));
    assert(serial.writeDeadlines.size() == 1);
    assert(serial.readDeadlines.size() == 2);
    assert(serial.writeDeadlines.front().set());
    assert(serial.writeDeadlines.front().expiresAt == serial.readDeadlines[0].expiresAt);
    assert(serial.writeDeadlines.front().expiresAt == serial.readDeadlines[1].expiresAt);
    assert(serial.readLimits == std::vector<std::size_t>({260, 260}));
    assert(frames.size() == 2);
    assert(frames[0].tx);
    assert(frames[0].bytes == request());
    assert(!frames[1].tx);
    assert(frames[1].bytes == expectedResponse);
    assert(!transport.serialFailed());
}

void reportsTypedWriteFailureAndShortWrite() {
    FakeSerialByteStream failedSerial;
    failedSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Failed,
        kGeneration,
        0,
        svm::transport::SerialErrorCategory::IoFailure,
        123));
    auto failedCurrentGeneration = kGeneration;
    svm::transport::SerialRtuTransport failedTransport(
        failedSerial,
        transportOptions(failedCurrentGeneration));

    const auto failed = failedTransport.exchange(request(), 100);

    assert(failed.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(failed.errorMessage == "Modbus 请求发送失败。 (native=123)");
    assert(failedTransport.serialFailed());
    assert(failedSerial.readDeadlines.empty());

    FakeSerialByteStream shortSerial;
    shortSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size() - 1));
    auto shortCurrentGeneration = kGeneration;
    svm::transport::SerialRtuTransport shortTransport(
        shortSerial,
        transportOptions(shortCurrentGeneration));

    const auto partial = shortTransport.exchange(request(), 100);

    assert(partial.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(partial.errorMessage == "Modbus 请求发送不完整。");
    assert(shortTransport.serialFailed());
    assert(shortSerial.readDeadlines.empty());
}

void reportsTypedReadTimeout() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Timeout,
        kGeneration,
        {},
        svm::transport::SerialErrorCategory::Timeout));
    auto currentGeneration = kGeneration;
    auto options = transportOptions(currentGeneration);
    options.timeoutErrorMessage = "custom timeout";
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::Timeout);
    assert(exchange.errorMessage == "custom timeout");
    assert(exchange.responseFrame.empty());
    assert(serial.readDeadlines.size() == 1);
    assert(serial.writeDeadlines.front().expiresAt == serial.readDeadlines.front().expiresAt);
    assert(!transport.serialFailed());
}

void observesCancellationDuringReadLoop() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration));
    bool cancelRequested = false;
    serial.onRead = [&cancelRequested] { cancelRequested = true; };
    auto currentGeneration = kGeneration;
    auto options = transportOptions(currentGeneration);
    options.shouldCancel = [&cancelRequested] { return cancelRequested; };
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(exchange.errorMessage == "扫描已取消。");
    assert(exchange.responseFrame.empty());
    assert(serial.readDeadlines.size() == 1);
    assert(transport.cancelObserved());
    assert(!transport.serialFailed());
}

void treatsTypedCancellationAsCancellation() {
    FakeSerialByteStream writeSerial;
    writeSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Cancelled,
        kGeneration,
        0,
        svm::transport::SerialErrorCategory::Cancelled));
    auto writeGeneration = kGeneration;
    svm::transport::SerialRtuTransport writeTransport(
        writeSerial,
        transportOptions(writeGeneration));

    const auto writeExchange = writeTransport.exchange(request(), 100);

    assert(writeExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(writeTransport.cancelObserved());
    assert(!writeTransport.serialFailed());

    FakeSerialByteStream readSerial;
    readSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    readSerial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Cancelled,
        kGeneration,
        {},
        svm::transport::SerialErrorCategory::Cancelled));
    auto readGeneration = kGeneration;
    svm::transport::SerialRtuTransport readTransport(
        readSerial,
        transportOptions(readGeneration));

    const auto readExchange = readTransport.exchange(request(), 100);

    assert(readExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(readTransport.cancelObserved());
    assert(!readTransport.serialFailed());

    FakeSerialByteStream closedSerial;
    closedSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::RejectedClosed,
        kGeneration,
        0,
        svm::transport::SerialErrorCategory::SessionClosed));
    auto closedGeneration = kGeneration;
    svm::transport::SerialRtuTransport closedTransport(
        closedSerial,
        transportOptions(closedGeneration));

    const auto closedExchange = closedTransport.exchange(request(), 100);

    assert(closedExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(closedTransport.cancelObserved());
    assert(!closedTransport.serialFailed());
}

void rejectsExpiredGenerationBeforeWrite() {
    FakeSerialByteStream serial;
    auto currentGeneration = kGeneration + 1;
    svm::transport::SerialRtuTransport transport(
        serial,
        transportOptions(currentGeneration));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(exchange.errorMessage == "串口会话已变化，Modbus 扫描已停止。");
    assert(exchange.responseFrame.empty());
    assert(serial.writes.empty());
    assert(serial.readDeadlines.empty());
    assert(transport.cancelObserved());
    assert(!transport.serialFailed());
}

void rejectsExpiredWriteResultGeneration() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration - 1,
        request().size()));
    auto currentGeneration = kGeneration;
    svm::transport::SerialRtuTransport transport(
        serial,
        transportOptions(currentGeneration));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(exchange.errorMessage == "串口会话已变化，Modbus 扫描已停止。");
    assert(exchange.responseFrame.empty());
    assert(serial.writes.size() == 1);
    assert(serial.readDeadlines.empty());
    assert(transport.cancelObserved());
    assert(!transport.serialFailed());
}

void rejectsExpiredReadResultWithoutPublishingResponse() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration - 1,
        response()));
    std::vector<bool> frameDirections;
    auto currentGeneration = kGeneration;
    auto options = transportOptions(currentGeneration);
    options.onFrame = [&frameDirections](bool tx, const svm::core::ByteBuffer&) {
        frameDirections.push_back(tx);
    };
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(exchange.errorMessage == "串口会话已变化，Modbus 扫描已停止。");
    assert(exchange.responseFrame.empty());
    assert(frameDirections == std::vector<bool>({true}));
    assert(serial.readDeadlines.size() == 1);
    assert(transport.cancelObserved());
    assert(!transport.serialFailed());
}

void rejectsGenerationChangeBeforeFinalResponsePublication() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        response()));
    std::vector<bool> frameDirections;
    int generationChecks = 0;
    svm::transport::SerialRtuTransportOptions options{
        .generation = kGeneration,
        .endpoint = kEndpoint,
        .generationIsCurrent = [&generationChecks](svm::transport::SerialSessionGeneration expected) {
            return expected == kGeneration && ++generationChecks < 8;
        },
    };
    options.onFrame = [&frameDirections](bool tx, const svm::core::ByteBuffer&) {
        frameDirections.push_back(tx);
    };
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(exchange.errorMessage == "串口会话已变化，Modbus 扫描已停止。");
    assert(exchange.responseFrame.empty());
    assert(frameDirections == std::vector<bool>({true}));
    assert(transport.cancelObserved());
    assert(!transport.serialFailed());
}

} // namespace

int main() {
    succeedsWithTypedChunkedResponseAndSharedDeadline();
    reportsTypedWriteFailureAndShortWrite();
    reportsTypedReadTimeout();
    observesCancellationDuringReadLoop();
    treatsTypedCancellationAsCancellation();
    rejectsExpiredGenerationBeforeWrite();
    rejectsExpiredWriteResultGeneration();
    rejectsExpiredReadResultWithoutPublishingResponse();
    rejectsGenerationChangeBeforeFinalResponsePublication();
    return 0;
}
