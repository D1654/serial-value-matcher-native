#include "transport/serial_rtu_transport.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
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

struct IoEvidence {
    svm::core::ByteBuffer payload;
    svm::transport::SerialOperationResult operation;
};

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
        if (onWrite) {
            onWrite();
        }
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
    std::function<void()> onWrite;
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
    auto firstRead = readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {expectedResponse.begin(), expectedResponse.begin() + 3});
    firstRead.operation.operation.requestId = 2;
    serial.readResults.push_back(std::move(firstRead));
    auto secondRead = readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {expectedResponse.begin() + 3, expectedResponse.end()});
    secondRead.operation.operation.requestId = 3;
    serial.readResults.push_back(std::move(secondRead));

    std::vector<IoEvidence> frames;
    auto currentGeneration = kGeneration;
    auto options = transportOptions(currentGeneration);
    options.nowUtc = [] { return std::string("2026-07-11T00:00:00Z"); };
    options.onIoEvidence = [&frames](
                          const svm::core::ByteBuffer& bytes,
                          const svm::transport::SerialOperationResult& operation) {
        frames.push_back({bytes, operation});
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
    assert(serial.readLimits == std::vector<std::size_t>({260, 257}));
    assert(frames.size() == 3);
    assert(frames[0].payload == request());
    assert(frames[0].operation.operation.kind == svm::transport::SerialOperationKind::Write);
    assert(frames[0].operation.operation.requestId == 1);
    assert(frames[0].operation.status == svm::transport::SerialOperationStatus::Succeeded);
    assert(frames[0].operation.byteCount == request().size());
    assert(frames[0].operation.endpoint == kEndpoint);
    assert(frames[1].payload == svm::core::ByteBuffer(expectedResponse.begin(), expectedResponse.begin() + 3));
    assert(frames[1].operation.operation.kind == svm::transport::SerialOperationKind::Read);
    assert(frames[1].operation.operation.requestId == 2);
    assert(frames[1].operation.status == svm::transport::SerialOperationStatus::Succeeded);
    assert(frames[1].operation.byteCount == 3);
    assert(frames[1].operation.endpoint == kEndpoint);
    assert(frames[2].payload == svm::core::ByteBuffer(expectedResponse.begin() + 3, expectedResponse.end()));
    assert(frames[2].operation.operation.kind == svm::transport::SerialOperationKind::Read);
    assert(frames[2].operation.operation.requestId == 3);
    assert(frames[2].operation.status == svm::transport::SerialOperationStatus::Succeeded);
    assert(frames[2].operation.byteCount == expectedResponse.size() - 3);
    assert(frames[2].operation.endpoint == kEndpoint);
    assert(!transport.serialFailed());
    assert(transport.serialFailure() == nullptr);
}

void usesResponseHeadersAndReturnsExactlyOneFrame() {
    FakeSerialByteStream exceptionSerial;
    exceptionSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    const svm::core::ByteBuffer exceptionBody{0x01, 0x83, 0x02};
    const auto exceptionFrame = svm::core::modbus::appendCrc16Modbus(exceptionBody);
    auto exceptionWithTrailing = exceptionFrame;
    const auto trailingFrame = response();
    exceptionWithTrailing.insert(exceptionWithTrailing.end(), trailingFrame.begin(), trailingFrame.end());
    exceptionSerial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        exceptionWithTrailing));
    auto exceptionGeneration = kGeneration;
    svm::transport::SerialRtuTransport exceptionTransport(
        exceptionSerial,
        transportOptions(exceptionGeneration));

    const auto exceptionExchange = exceptionTransport.exchange(request(), 100);

    assert(exceptionExchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(exceptionExchange.responseFrame == exceptionFrame);
    const auto parsedException = svm::core::modbus::parseReadResponse(
        exceptionExchange.responseFrame,
        1,
        0x03,
        0,
        1);
    assert(parsedException.isException);
    assert(parsedException.exceptionCode == 0x02);

    FakeSerialByteStream wrongFunctionSerial;
    wrongFunctionSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    const svm::core::ByteBuffer wrongFunctionBody{0x01, 0x04, 0x02, 0x12, 0x34};
    const auto wrongFunctionFrame = svm::core::modbus::appendCrc16Modbus(wrongFunctionBody);
    wrongFunctionSerial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        wrongFunctionFrame));
    auto wrongFunctionGeneration = kGeneration;
    svm::transport::SerialRtuTransport wrongFunctionTransport(
        wrongFunctionSerial,
        transportOptions(wrongFunctionGeneration));

    const auto wrongFunctionExchange = wrongFunctionTransport.exchange(request(), 100);

    assert(wrongFunctionExchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(wrongFunctionExchange.responseFrame == wrongFunctionFrame);
    const auto parsedWrongFunction = svm::core::modbus::parseReadResponse(
        wrongFunctionExchange.responseFrame,
        1,
        0x03,
        0,
        1);
    assert(!parsedWrongFunction.ok);
    assert(parsedWrongFunction.errorMessage == "Response function code does not match expected function code.");

    FakeSerialByteStream byteCountSerial;
    byteCountSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    const svm::core::ByteBuffer byteCountBody{0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78};
    const auto byteCountFrame = svm::core::modbus::appendCrc16Modbus(byteCountBody);
    byteCountSerial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {byteCountFrame.begin(), byteCountFrame.begin() + 7}));
    byteCountSerial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {byteCountFrame.begin() + 7, byteCountFrame.end()}));
    auto byteCountGeneration = kGeneration;
    svm::transport::SerialRtuTransport byteCountTransport(
        byteCountSerial,
        transportOptions(byteCountGeneration));

    const auto byteCountExchange = byteCountTransport.exchange(request(), 100);

    assert(byteCountExchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(byteCountExchange.responseFrame == byteCountFrame);
    assert(byteCountSerial.readDeadlines.size() == 2);
    const auto parsedByteCount = svm::core::modbus::parseReadResponse(
        byteCountExchange.responseFrame,
        1,
        0x03,
        0,
        1);
    assert(!parsedByteCount.ok);
    assert(parsedByteCount.errorMessage == "Response register count does not match expected quantity.");
}

void capsUnknownFunctionResponsesAtOneRtuFrame() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));

    svm::core::ByteBuffer noise{0x01, 0x10};
    while (noise.size() < 260) {
        noise.push_back(0xA5);
        if (noise.size() >= 5 && svm::core::modbus::validateRtuFrame(noise).ok) {
            noise.back() ^= 0x01;
        }
        assert(!svm::core::modbus::validateRtuFrame(noise).ok);
    }
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {noise.begin(), noise.begin() + 130}));
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {noise.begin() + 130, noise.end()}));
    auto currentGeneration = kGeneration;
    svm::transport::SerialRtuTransport transport(serial, transportOptions(currentGeneration));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(exchange.responseFrame == noise);
    assert(serial.readLimits == std::vector<std::size_t>({260, 130}));
    assert(serial.readResults.empty());
    assert(!transport.serialFailed());
}

void acceptsShortUnknownFunctionResponseAndTrimsTrailingBytes() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    const svm::core::ByteBuffer unknownBody{0x01, 0x10};
    const svm::core::ByteBuffer unknownFrame =
        svm::core::modbus::appendCrc16Modbus(unknownBody);
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        {unknownFrame.begin(), unknownFrame.begin() + 3}));
    svm::core::ByteBuffer remainder{unknownFrame.begin() + 3, unknownFrame.end()};
    const svm::core::ByteBuffer trailingFrame = response();
    remainder.insert(remainder.end(), trailingFrame.begin(), trailingFrame.end());
    serial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        std::move(remainder)));
    auto currentGeneration = kGeneration;
    svm::transport::SerialRtuTransport transport(serial, transportOptions(currentGeneration));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::Success);
    assert(exchange.responseFrame == unknownFrame);
    assert(serial.readLimits == std::vector<std::size_t>({260, 257}));
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
    std::vector<IoEvidence> failedEvidence;
    auto failedOptions = transportOptions(failedCurrentGeneration);
    failedOptions.onIoEvidence = [&failedEvidence](
                                     const svm::core::ByteBuffer& payload,
                                     const svm::transport::SerialOperationResult& operation) {
        failedEvidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport failedTransport(
        failedSerial,
        std::move(failedOptions));

    const auto failed = failedTransport.exchange(request(), 100);

    assert(failed.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(failed.errorMessage == "Modbus 请求发送失败。 (native=123)");
    assert(failedTransport.serialFailed());
    assert(failedTransport.serialFailure() != nullptr);
    assert(failedTransport.serialFailure()->operation.kind == svm::transport::SerialOperationKind::Write);
    assert(failedTransport.serialFailure()->status == svm::transport::SerialOperationStatus::Failed);
    assert(failedTransport.serialFailure()->error.category == svm::transport::SerialErrorCategory::IoFailure);
    assert(failedTransport.serialFailure()->error.nativeCode == 123);
    assert(failedEvidence.size() == 1);
    assert(failedEvidence.front().payload.empty());
    assert(failedEvidence.front().operation.error.nativeCode == 123);
    assert(failedSerial.readDeadlines.empty());

    FakeSerialByteStream shortSerial;
    shortSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size() - 1));
    auto shortCurrentGeneration = kGeneration;
    std::vector<IoEvidence> shortEvidence;
    auto shortOptions = transportOptions(shortCurrentGeneration);
    shortOptions.onIoEvidence = [&shortEvidence](
                                    const svm::core::ByteBuffer& payload,
                                    const svm::transport::SerialOperationResult& operation) {
        shortEvidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport shortTransport(
        shortSerial,
        std::move(shortOptions));

    const auto partial = shortTransport.exchange(request(), 100);

    assert(partial.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(partial.errorMessage == "Modbus 请求发送不完整。");
    assert(shortTransport.serialFailed());
    assert(shortTransport.serialFailure() != nullptr);
    assert(shortTransport.serialFailure()->status == svm::transport::SerialOperationStatus::Failed);
    assert(shortTransport.serialFailure()->byteCount == request().size() - 1);
    assert(shortTransport.serialFailure()->error.category == svm::transport::SerialErrorCategory::IoFailure);
    assert(shortEvidence.size() == 1);
    assert(shortEvidence.front().payload.size() == request().size() - 1);
    assert(shortEvidence.front().operation.status == svm::transport::SerialOperationStatus::Failed);
    assert(shortSerial.readDeadlines.empty());
}

void reportsTypedReadFailureEvidence() {
    FakeSerialByteStream serial;
    serial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    auto readFailure = readResult(
        svm::transport::SerialOperationStatus::Disconnected,
        kGeneration,
        {},
        svm::transport::SerialErrorCategory::Disconnected);
    readFailure.operation.error.nativeCode = 1167;
    serial.readResults.push_back(std::move(readFailure));
    auto currentGeneration = kGeneration;
    std::vector<IoEvidence> evidence;
    auto options = transportOptions(currentGeneration);
    options.onIoEvidence = [&evidence](
                               const svm::core::ByteBuffer& payload,
                               const svm::transport::SerialOperationResult& operation) {
        evidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport transport(
        serial,
        std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(transport.serialFailed());
    assert(transport.serialFailure() != nullptr);
    assert(transport.serialFailure()->operation.kind == svm::transport::SerialOperationKind::Read);
    assert(transport.serialFailure()->status == svm::transport::SerialOperationStatus::Disconnected);
    assert(transport.serialFailure()->error.category == svm::transport::SerialErrorCategory::Disconnected);
    assert(transport.serialFailure()->error.nativeCode == 1167);
    assert(evidence.size() == 2);
    assert(evidence[0].payload == request());
    assert(evidence[0].operation.status == svm::transport::SerialOperationStatus::Succeeded);
    assert(evidence[1].payload.empty());
    assert(evidence[1].operation.status == svm::transport::SerialOperationStatus::Disconnected);
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
    std::vector<IoEvidence> evidence;
    options.onIoEvidence = [&evidence](
                               const svm::core::ByteBuffer& payload,
                               const svm::transport::SerialOperationResult& operation) {
        evidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::Timeout);
    assert(exchange.errorMessage == "custom timeout");
    assert(exchange.responseFrame.empty());
    assert(serial.readDeadlines.size() == 1);
    assert(serial.writeDeadlines.front().expiresAt == serial.readDeadlines.front().expiresAt);
    assert(!transport.serialFailed());
    assert(transport.serialFailure() == nullptr);
    assert(evidence.size() == 2);
    assert(evidence[0].payload == request());
    assert(evidence[1].payload.empty());
    assert(evidence[1].operation.status == svm::transport::SerialOperationStatus::Timeout);
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

void cancellationDuringNativeIoWinsOverNativeFailure() {
    FakeSerialByteStream writeSerial;
    writeSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Failed,
        kGeneration,
        0,
        svm::transport::SerialErrorCategory::NativeFailure,
        995));
    bool writeCancelled = false;
    writeSerial.onWrite = [&writeCancelled] { writeCancelled = true; };
    auto writeGeneration = kGeneration;
    std::vector<IoEvidence> writeEvidence;
    auto writeOptions = transportOptions(writeGeneration);
    writeOptions.shouldCancel = [&writeCancelled] { return writeCancelled; };
    writeOptions.onIoEvidence = [&writeEvidence](
                                    const svm::core::ByteBuffer& payload,
                                    const svm::transport::SerialOperationResult& operation) {
        writeEvidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport writeTransport(writeSerial, std::move(writeOptions));

    const auto writeExchange = writeTransport.exchange(request(), 100);

    assert(writeExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(writeExchange.errorMessage == "扫描已取消。");
    assert(writeTransport.cancelObserved());
    assert(!writeTransport.serialFailed());
    assert(writeTransport.serialFailure() == nullptr);
    assert(writeEvidence.size() == 1);
    assert(writeEvidence.front().payload.empty());
    assert(writeEvidence.front().operation.error.nativeCode == 995);

    FakeSerialByteStream readSerial;
    readSerial.writeResults.push_back(terminalResult(
        svm::transport::SerialOperationKind::Write,
        svm::transport::SerialOperationStatus::Succeeded,
        kGeneration,
        request().size()));
    readSerial.readResults.push_back(readResult(
        svm::transport::SerialOperationStatus::Failed,
        kGeneration,
        {},
        svm::transport::SerialErrorCategory::NativeFailure));
    readSerial.readResults.front().operation.error.nativeCode = 995;
    bool readCancelled = false;
    readSerial.onRead = [&readCancelled] { readCancelled = true; };
    auto readGeneration = kGeneration;
    std::vector<IoEvidence> readEvidence;
    auto readOptions = transportOptions(readGeneration);
    readOptions.shouldCancel = [&readCancelled] { return readCancelled; };
    readOptions.onIoEvidence = [&readEvidence](
                                   const svm::core::ByteBuffer& payload,
                                   const svm::transport::SerialOperationResult& operation) {
        readEvidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport readTransport(readSerial, std::move(readOptions));

    const auto readExchange = readTransport.exchange(request(), 100);

    assert(readExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(readExchange.errorMessage == "扫描已取消。");
    assert(readTransport.cancelObserved());
    assert(!readTransport.serialFailed());
    assert(readTransport.serialFailure() == nullptr);
    assert(readEvidence.size() == 2);
    assert(readEvidence[0].payload == request());
    assert(readEvidence[1].payload.empty());
    assert(readEvidence[1].operation.error.nativeCode == 995);
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
    std::vector<IoEvidence> writeEvidence;
    auto writeOptions = transportOptions(writeGeneration);
    writeOptions.onIoEvidence = [&writeEvidence](
                                    const svm::core::ByteBuffer& payload,
                                    const svm::transport::SerialOperationResult& operation) {
        writeEvidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport writeTransport(
        writeSerial,
        std::move(writeOptions));

    const auto writeExchange = writeTransport.exchange(request(), 100);

    assert(writeExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(writeTransport.cancelObserved());
    assert(!writeTransport.serialFailed());
    assert(writeEvidence.size() == 1);
    assert(writeEvidence.front().payload.empty());
    assert(writeEvidence.front().operation.status == svm::transport::SerialOperationStatus::Cancelled);

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
    std::vector<IoEvidence> readEvidence;
    auto readOptions = transportOptions(readGeneration);
    readOptions.onIoEvidence = [&readEvidence](
                                   const svm::core::ByteBuffer& payload,
                                   const svm::transport::SerialOperationResult& operation) {
        readEvidence.push_back({payload, operation});
    };
    svm::transport::SerialRtuTransport readTransport(
        readSerial,
        std::move(readOptions));

    const auto readExchange = readTransport.exchange(request(), 100);

    assert(readExchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(readTransport.cancelObserved());
    assert(!readTransport.serialFailed());
    assert(readEvidence.size() == 2);
    assert(readEvidence[0].payload == request());
    assert(readEvidence[1].payload.empty());
    assert(readEvidence[1].operation.status == svm::transport::SerialOperationStatus::Cancelled);

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
    options.onIoEvidence = [&frameDirections](
                          const svm::core::ByteBuffer&,
                          const svm::transport::SerialOperationResult& operation) {
        frameDirections.push_back(operation.operation.kind == svm::transport::SerialOperationKind::Write);
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
    options.onIoEvidence = [&frameDirections](
                          const svm::core::ByteBuffer&,
                          const svm::transport::SerialOperationResult& operation) {
        frameDirections.push_back(operation.operation.kind == svm::transport::SerialOperationKind::Write);
    };
    svm::transport::SerialRtuTransport transport(serial, std::move(options));

    const auto exchange = transport.exchange(request(), 100);

    assert(exchange.status == svm::core::modbus::RtuTransportExchangeStatus::TransportError);
    assert(exchange.errorMessage == "串口会话已变化，Modbus 扫描已停止。");
    assert(exchange.responseFrame.empty());
    assert(frameDirections == std::vector<bool>({true, false}));
    assert(transport.cancelObserved());
    assert(!transport.serialFailed());
}

} // namespace

int main() {
    succeedsWithTypedChunkedResponseAndSharedDeadline();
    usesResponseHeadersAndReturnsExactlyOneFrame();
    capsUnknownFunctionResponsesAtOneRtuFrame();
    acceptsShortUnknownFunctionResponseAndTrimsTrailingBytes();
    reportsTypedWriteFailureAndShortWrite();
    reportsTypedReadFailureEvidence();
    reportsTypedReadTimeout();
    observesCancellationDuringReadLoop();
    cancellationDuringNativeIoWinsOverNativeFailure();
    treatsTypedCancellationAsCancellation();
    rejectsExpiredGenerationBeforeWrite();
    rejectsExpiredWriteResultGeneration();
    rejectsExpiredReadResultWithoutPublishingResponse();
    rejectsGenerationChangeBeforeFinalResponsePublication();
    return 0;
}
