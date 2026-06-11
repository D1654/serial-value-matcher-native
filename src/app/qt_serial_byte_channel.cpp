#include "app/qt_serial_byte_channel.h"

#include <QIODevice>

#include <utility>

namespace svm::app {

QtSerialByteChannel::QtSerialByteChannel(transport::SerialOpenOptions options)
    : options_(std::move(options))
{
}

QtSerialByteChannel::~QtSerialByteChannel()
{
    if (port_.isOpen()) {
        port_.close();
    }
}

bool QtSerialByteChannel::open()
{
    if (options_.portName.trimmed().isEmpty()) {
        lastErrorText_ = QStringLiteral("未选择串口，无法执行 Modbus 扫描。");
        return false;
    }

    port_.setPortName(options_.portName);
    port_.setBaudRate(options_.baudRate);
    port_.setDataBits(options_.dataBits);
    port_.setParity(options_.parity);
    port_.setStopBits(options_.stopBits);
    port_.setFlowControl(options_.flowControl);

    if (!port_.open(QIODevice::ReadWrite)) {
        lastErrorText_ = QStringLiteral("打开串口失败：%1").arg(port_.errorString());
        return false;
    }

    if (!port_.setDataTerminalReady(options_.dataTerminalReady)) {
        lastErrorText_ = QStringLiteral("设置 DTR 失败：%1").arg(port_.errorString());
        port_.close();
        return false;
    }
    if (!port_.setRequestToSend(options_.requestToSend)) {
        lastErrorText_ = QStringLiteral("设置 RTS 失败：%1").arg(port_.errorString());
        port_.close();
        return false;
    }
    lastErrorText_.clear();
    return true;
}

bool QtSerialByteChannel::isOpen() const
{
    return port_.isOpen();
}

QString QtSerialByteChannel::endpoint() const
{
    return options_.portName;
}

qint64 QtSerialByteChannel::writeBytes(const QByteArray& payload)
{
    if (!port_.isOpen()) {
        lastErrorText_ = QStringLiteral("串口未打开，无法发送 Modbus RTU 请求。");
        return -1;
    }
    const qint64 written = port_.write(payload);
    if (written < 0) {
        lastErrorText_ = QStringLiteral("发送 Modbus RTU 请求失败：%1").arg(port_.errorString());
        return written;
    }
    if (!port_.waitForBytesWritten(1000)) {
        lastErrorText_ = QStringLiteral("等待 Modbus RTU 请求写入串口超时：%1").arg(port_.errorString());
        return -1;
    }
    if (written != payload.size()) {
        lastErrorText_ = QStringLiteral("Modbus RTU 请求发送不完整：应发送 %1 字节，实际发送 %2 字节。")
            .arg(payload.size())
            .arg(written);
    } else {
        lastErrorText_.clear();
    }
    return written;
}

bool QtSerialByteChannel::waitForReadyRead(int timeoutMs)
{
    if (!port_.isOpen()) {
        lastErrorText_ = QStringLiteral("串口未打开，无法等待 Modbus RTU 响应。");
        return false;
    }
    const bool ready = port_.waitForReadyRead(timeoutMs);
    if (!ready && port_.error() != QSerialPort::NoError && port_.error() != QSerialPort::TimeoutError) {
        lastErrorText_ = QStringLiteral("等待 Modbus RTU 响应失败：%1").arg(port_.errorString());
    }
    return ready;
}

QByteArray QtSerialByteChannel::readAvailable()
{
    if (!port_.isOpen()) {
        lastErrorText_ = QStringLiteral("串口未打开，无法读取 Modbus RTU 响应。");
        return {};
    }
    lastErrorText_.clear();
    return port_.readAll();
}

QString QtSerialByteChannel::lastErrorText() const
{
    return lastErrorText_;
}

} // namespace svm::app
