#include "transport/serial_error_translator.h"

namespace svm::transport {

QString SerialErrorTranslator::translate(QSerialPort::SerialPortError error, const QString& originalErrorText) {
    switch (error) {
    case QSerialPort::NoError:
        return {};
    case QSerialPort::DeviceNotFoundError:
        return withOriginal(QStringLiteral("没有找到这个串口设备。可能是设备已拔出、端口号变化，或驱动没有正确加载。请刷新端口列表后重试。"), originalErrorText);
    case QSerialPort::PermissionError:
        return withOriginal(QStringLiteral("串口被占用或权限不足。请先关闭其他串口助手/烧录工具，或检查当前用户是否有访问串口的权限。"), originalErrorText);
    case QSerialPort::OpenError:
        return withOriginal(QStringLiteral("串口打开失败。请检查端口是否被其他程序占用，或设备是否刚刚断开又重连。"), originalErrorText);
    case QSerialPort::NotOpenError:
        return withOriginal(QStringLiteral("串口尚未打开，当前操作无法执行。请先连接串口。"), originalErrorText);
    case QSerialPort::WriteError:
        return withOriginal(QStringLiteral("串口发送失败。设备可能已断开，或驱动暂时无法写入数据。"), originalErrorText);
    case QSerialPort::ReadError:
        return withOriginal(QStringLiteral("串口读取失败。设备可能已断开，或驱动暂时无法读取数据。"), originalErrorText);
    case QSerialPort::ResourceError:
        return withOriginal(QStringLiteral("串口资源错误。设备可能已被拔出、驱动重置，或连接链路异常。请重新插拔设备并刷新端口。"), originalErrorText);
    case QSerialPort::UnsupportedOperationError:
        return withOriginal(QStringLiteral("当前系统或驱动不支持这个串口操作。请检查流控、DTR/RTS 或特殊串口参数。"), originalErrorText);
    case QSerialPort::TimeoutError:
        return withOriginal(QStringLiteral("串口操作超时。设备可能没有响应，或当前波特率/接线/协议参数不匹配。"), originalErrorText);
    default:
        return withOriginal(QStringLiteral("发生未知串口错误。请检查设备连接、驱动状态和端口占用情况。"), originalErrorText);
    }
}


QString SerialErrorTranslator::controlLineFailureMessage(const QString& signalName, const QString& originalErrorText) {
    return withOriginal(QStringLiteral("设置 %1 信号失败。当前 USB 转串口芯片、驱动或系统可能不支持该控制线，也可能是设备已断开。请尝试关闭该选项或重新插拔设备。").arg(signalName),
        originalErrorText);
}

QString SerialErrorTranslator::writeResultMessage(qint64 expectedBytes, qint64 writtenBytes, const QString& originalErrorText) {
    if (expectedBytes <= 0 || writtenBytes == expectedBytes) {
        return {};
    }

    if (writtenBytes < 0) {
        return withOriginal(QStringLiteral("串口发送失败，驱动未接受待发送数据。设备可能已断开，或端口状态异常。"), originalErrorText);
    }

    if (writtenBytes == 0) {
        return withOriginal(QStringLiteral("串口发送失败，驱动没有写入任何字节。请检查设备连接、流控设置和端口状态。"), originalErrorText);
    }

    return withOriginal(QStringLiteral("串口发送不完整：应发送 %1 字节，实际只写入 %2 字节。设备可能已断开、缓冲区已满，或流控阻止继续发送。")
            .arg(expectedBytes)
            .arg(writtenBytes),
        originalErrorText);
}

QString SerialErrorTranslator::withOriginal(QString message, const QString& originalErrorText) {
    if (originalErrorText.trimmed().isEmpty()) {
        return message;
    }

    return QStringLiteral("%1\n原始错误：%2").arg(message, originalErrorText.trimmed());
}

} // namespace svm::transport
