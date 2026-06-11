#include <QtTest/QtTest>
#include <QSignalSpy>

#include "capture/capture_bus.h"
#include "capture/raw_io_event.h"
#include "modbus/modbus_rtu_byte_channel.h"
#include "modbus/modbus_rtu_serial_transport.h"

#include <utility>

namespace {

class FakeByteChannel final : public svm::modbus::ModbusRtuByteChannel {
public:
    bool isOpen() const override { return open; }
    QString endpoint() const override { return endpointName; }
    QString lastErrorText() const override { return errorText; }

    qint64 writeBytes(const QByteArray& payload) override {
        writtenPayloads.append(payload);
        if (partialWrite) {
            errorText = QStringLiteral("测试通道只写入了部分字节。");
            return payload.size() - 1;
        }
        if (writeErrorAfterFullWrite) {
            errorText = QStringLiteral("等待 Modbus RTU 请求写入串口超时。");
            return payload.size();
        }
        errorText.clear();
        return payload.size();
    }

    bool waitForReadyRead(int timeoutMs) override {
        timeoutValues.append(timeoutMs);
        return waitSucceeds && !responseChunks.isEmpty();
    }

    QByteArray readAvailable() override {
        if (responseChunks.isEmpty()) {
            return {};
        }
        return responseChunks.takeFirst();
    }

    void enqueueResponseChunk(QByteArray chunk) {
        responseChunks.append(std::move(chunk));
    }

    QByteArray fullResponse() const {
        QByteArray combined;
        for (const auto& chunk : originalChunks) {
            combined.append(chunk);
        }
        return combined;
    }

    void setResponseChunks(std::initializer_list<QByteArray> chunks) {
        responseChunks.clear();
        originalChunks.clear();
        for (const auto& chunk : chunks) {
            responseChunks.append(chunk);
            originalChunks.append(chunk);
        }
    }

    bool open = true;
    bool waitSucceeds = true;
    bool partialWrite = false;
    bool writeErrorAfterFullWrite = false;
    QString endpointName = QStringLiteral("fake://serial");
    QString errorText;
    QVector<QByteArray> responseChunks;
    QVector<QByteArray> originalChunks;
    QVector<QByteArray> writtenPayloads;
    QVector<int> timeoutValues;
};

svm::capture::RawIoEvent eventAt(const QSignalSpy& spy, int index) {
    return qvariant_cast<svm::capture::RawIoEvent>(spy.at(index).at(0));
}

} // namespace

class ModbusRtuSerialTransportTests final : public QObject {
    Q_OBJECT

private slots:
    void exchangeWritesRequestReadsChunkedResponseAndPublishesCaptureEvents() {
        FakeByteChannel channel;
        channel.setResponseChunks({QByteArray::fromHex("0103"), QByteArray::fromHex("0200017984")});
        svm::capture::CaptureBus captureBus;
        QSignalSpy spy(&captureBus, &svm::capture::CaptureBus::eventCaptured);
        svm::modbus::ModbusRtuSerialTransportOptions options;
        options.sessionId = QStringLiteral("scan-real-boundary");
        svm::modbus::ModbusRtuSerialTransport transport(channel, &captureBus, options);

        const QByteArray request = QByteArray::fromHex("010300000001840A");
        const auto exchange = transport.exchange(request, 500);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::Success);
        QCOMPARE(exchange.requestFrame, request);
        QCOMPARE(exchange.responseFrame, QByteArray::fromHex("01030200017984"));
        QCOMPARE(exchange.endpoint, QStringLiteral("fake://serial"));
        QCOMPARE(channel.writtenPayloads, QVector<QByteArray>({request}));
        QCOMPARE(channel.timeoutValues.size(), 2);
        QVERIFY(channel.timeoutValues[0] <= 500);
        QCOMPARE(spy.count(), 2);
        const auto tx = eventAt(spy, 0);
        const auto rx = eventAt(spy, 1);
        QCOMPARE(tx.sessionId, QStringLiteral("scan-real-boundary"));
        QCOMPARE(tx.direction, svm::capture::Direction::Tx);
        QCOMPARE(tx.payload, request);
        QCOMPARE(rx.direction, svm::capture::Direction::Rx);
        QCOMPARE(rx.payload, QByteArray::fromHex("01030200017984"));
    }

    void exchangeReturnsFiveByteExceptionFrameAsCompleteResponse() {
        FakeByteChannel channel;
        channel.setResponseChunks({QByteArray::fromHex("018302"), QByteArray::fromHex("C0F1")});
        svm::modbus::ModbusRtuSerialTransport transport(channel, nullptr, {});

        const auto exchange = transport.exchange(QByteArray::fromHex("010300000001840A"), 500);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::Success);
        QCOMPARE(exchange.responseFrame, QByteArray::fromHex("018302C0F1"));
        QCOMPARE(channel.timeoutValues.size(), 2);
    }

    void timeoutKeepsRequestFrameAndPublishesOnlyTxWhenNoResponse() {
        FakeByteChannel channel;
        channel.waitSucceeds = false;
        svm::capture::CaptureBus captureBus;
        QSignalSpy spy(&captureBus, &svm::capture::CaptureBus::eventCaptured);
        svm::modbus::ModbusRtuSerialTransport transport(channel, &captureBus, {});

        const QByteArray request = QByteArray::fromHex("010300000001840A");
        const auto exchange = transport.exchange(request, 250);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::Timeout);
        QCOMPARE(exchange.requestFrame, request);
        QVERIFY(exchange.responseFrame.isEmpty());
        QVERIFY(exchange.errorMessage.contains(QStringLiteral("超时")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(eventAt(spy, 0).direction, svm::capture::Direction::Tx);
    }

    void timeoutKeepsPartialResponseAndPublishesRxTrace() {
        FakeByteChannel channel;
        channel.setResponseChunks({QByteArray::fromHex("010302")});
        svm::capture::CaptureBus captureBus;
        QSignalSpy spy(&captureBus, &svm::capture::CaptureBus::eventCaptured);
        svm::modbus::ModbusRtuSerialTransport transport(channel, &captureBus, {});

        const auto exchange = transport.exchange(QByteArray::fromHex("010300000001840A"), 100);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::Timeout);
        QCOMPARE(exchange.responseFrame, QByteArray::fromHex("010302"));
        QVERIFY(exchange.errorMessage.contains(QStringLiteral("超时")));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(eventAt(spy, 1).direction, svm::capture::Direction::Rx);
        QCOMPARE(eventAt(spy, 1).payload, QByteArray::fromHex("010302"));
    }

    void closedChannelReturnsTransportErrorWithoutWriting() {
        FakeByteChannel channel;
        channel.open = false;
        channel.errorText = QStringLiteral("串口未打开");
        svm::capture::CaptureBus captureBus;
        QSignalSpy spy(&captureBus, &svm::capture::CaptureBus::eventCaptured);
        svm::modbus::ModbusRtuSerialTransport transport(channel, &captureBus, {});

        const auto exchange = transport.exchange(QByteArray::fromHex("010300000001840A"), 100);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::TransportError);
        QVERIFY(exchange.errorMessage.contains(QStringLiteral("串口未打开")));
        QCOMPARE(channel.writtenPayloads.size(), 0);
        QCOMPARE(spy.count(), 0);
    }

    void partialWriteReturnsTransportErrorBeforeWaitingResponse() {
        FakeByteChannel channel;
        channel.partialWrite = true;
        svm::capture::CaptureBus captureBus;
        QSignalSpy spy(&captureBus, &svm::capture::CaptureBus::eventCaptured);
        svm::modbus::ModbusRtuSerialTransport transport(channel, &captureBus, {});

        const auto exchange = transport.exchange(QByteArray::fromHex("010300000001840A"), 100);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::TransportError);
        QVERIFY(exchange.errorMessage.contains(QStringLiteral("部分字节")));
        QCOMPARE(channel.timeoutValues.size(), 0);
        QCOMPARE(spy.count(), 0);
    }

    void fullWriteWithChannelErrorReturnsTransportErrorBeforeWaitingResponse() {
        FakeByteChannel channel;
        channel.writeErrorAfterFullWrite = true;
        svm::capture::CaptureBus captureBus;
        QSignalSpy spy(&captureBus, &svm::capture::CaptureBus::eventCaptured);
        svm::modbus::ModbusRtuSerialTransport transport(channel, &captureBus, {});

        const auto exchange = transport.exchange(QByteArray::fromHex("010300000001840A"), 100);

        QCOMPARE(exchange.status, svm::modbus::ModbusTransportStatus::TransportError);
        QVERIFY(exchange.errorMessage.contains(QStringLiteral("写入串口超时")));
        QCOMPARE(channel.timeoutValues.size(), 0);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(ModbusRtuSerialTransportTests)
#include "modbus_rtu_serial_transport_tests.moc"
