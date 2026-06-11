#include "modbus/modbus_rtu_serial_transport.h"

#include "capture/raw_io_event.h"

#include <QDateTime>
#include <QElapsedTimer>

#include <algorithm>
#include <utility>

namespace svm::modbus {
namespace {

quint8 byteAt(const QByteArray& data, int index) {
    return static_cast<quint8>(data.at(index));
}

int expectedResponseLength(const QByteArray& requestFrame, const QByteArray& responseFrame) {
    if (requestFrame.size() < 2 || responseFrame.size() < 2) {
        return -1;
    }

    const quint8 expectedFunction = byteAt(requestFrame, 1);
    const quint8 responseFunction = byteAt(responseFrame, 1);
    if (responseFunction == static_cast<quint8>(expectedFunction | 0x80u)) {
        return 5;
    }

    if (responseFunction == expectedFunction) {
        if (responseFrame.size() < 3) {
            return -1;
        }
        const int byteCount = byteAt(responseFrame, 2);
        return 5 + byteCount;
    }

    // Unknown function code: once a minimal RTU frame is present, return it so the parser can
    // produce the structured mismatch diagnostic instead of hiding the response behind timeout.
    return responseFrame.size() >= 5 ? responseFrame.size() : -1;
}

} // namespace

ModbusRtuSerialTransport::ModbusRtuSerialTransport(
    ModbusRtuByteChannel& channel,
    capture::CaptureBus* captureBus,
    ModbusRtuSerialTransportOptions options)
    : channel_(channel), captureBus_(captureBus), options_(std::move(options)) {}

ModbusTransportExchange ModbusRtuSerialTransport::exchange(const QByteArray& requestFrame, int responseTimeoutMs) {
    ModbusTransportExchange result;
    result.requestFrame = requestFrame;
    result.endpoint = channel_.endpoint();
    result.sentAtUtc = QDateTime::currentDateTimeUtc();

    if (requestFrame.isEmpty()) {
        result.status = ModbusTransportStatus::TransportError;
        result.errorMessage = QStringLiteral("Modbus RTU 请求为空，无法发送。");
        result.receivedAtUtc = QDateTime::currentDateTimeUtc();
        return result;
    }

    if (!channel_.isOpen()) {
        result.status = ModbusTransportStatus::TransportError;
        const QString channelError = channel_.lastErrorText();
        result.errorMessage = channelError.isEmpty()
            ? QStringLiteral("串口通道未打开，无法执行 Modbus RTU 请求。")
            : channelError;
        result.receivedAtUtc = QDateTime::currentDateTimeUtc();
        return result;
    }

    const qint64 written = channel_.writeBytes(requestFrame);
    const QString writeError = channel_.lastErrorText();
    if (written != requestFrame.size() || !writeError.isEmpty()) {
        result.status = ModbusTransportStatus::TransportError;
        result.errorMessage = writeError.isEmpty()
            ? QStringLiteral("Modbus RTU 请求发送不完整。")
            : writeError;
        result.receivedAtUtc = QDateTime::currentDateTimeUtc();
        return result;
    }

    publishRawEvent(capture::Direction::Tx, requestFrame, result.sentAtUtc);

    QByteArray responseBuffer;
    QElapsedTimer timer;
    timer.start();
    const int safeTimeoutMs = std::max(0, responseTimeoutMs);

    while (true) {
        const int expectedLength = expectedResponseLength(requestFrame, responseBuffer);
        if (expectedLength > 0 && responseBuffer.size() >= expectedLength) {
            result.responseFrame = responseBuffer.left(expectedLength);
            result.receivedAtUtc = QDateTime::currentDateTimeUtc();
            result.status = ModbusTransportStatus::Success;
            publishRawEvent(capture::Direction::Rx, result.responseFrame, result.receivedAtUtc);
            return result;
        }

        const int remainingMs = safeTimeoutMs - static_cast<int>(timer.elapsed());
        if (remainingMs <= 0) {
            break;
        }

        if (!channel_.waitForReadyRead(remainingMs)) {
            break;
        }

        const QByteArray chunk = channel_.readAvailable();
        if (!chunk.isEmpty()) {
            responseBuffer.append(chunk);
        }
    }

    result.status = ModbusTransportStatus::Timeout;
    result.responseFrame = responseBuffer;
    result.receivedAtUtc = QDateTime::currentDateTimeUtc();
    result.errorMessage = responseBuffer.isEmpty()
        ? QStringLiteral("等待 Modbus RTU 响应超时。")
        : QStringLiteral("等待完整 Modbus RTU 响应超时，已收到 %1 字节部分响应。").arg(responseBuffer.size());
    publishRawEvent(capture::Direction::Rx, result.responseFrame, result.receivedAtUtc);
    return result;
}

void ModbusRtuSerialTransport::publishRawEvent(
    capture::Direction direction,
    const QByteArray& payload,
    const QDateTime& timestampUtc) {
    if (!captureBus_ || !options_.publishCaptureEvents || payload.isEmpty()) {
        return;
    }

    capture::RawIoEvent event;
    event.sessionId = options_.sessionId;
    event.direction = direction;
    event.timestampUtc = timestampUtc;
    event.endpoint = channel_.endpoint();
    event.payload = payload;
    captureBus_->publish(event);
}

} // namespace svm::modbus
