#include "protocol/payload_codec.h"

#include <QRegularExpression>

namespace svm::protocol {

EncodeResult PayloadCodec::encode(QString input, PayloadMode mode, LineEnding lineEnding) {
    EncodeResult result;
    if (mode == PayloadMode::Text) {
        result.ok = true;
        result.payload = input.toUtf8();
    } else {
        result = encodeHex(input);
        if (!result.ok) {
            return result;
        }
    }

    result.payload.append(lineEndingBytes(lineEnding));
    return result;
}

QString PayloadCodec::bytesToHex(const QByteArray& payload) {
    return QString::fromLatin1(payload.toHex(' ').toUpper());
}

QByteArray PayloadCodec::lineEndingBytes(LineEnding lineEnding) {
    switch (lineEnding) {
    case LineEnding::None:
        return {};
    case LineEnding::Cr:
        return QByteArray("\r", 1);
    case LineEnding::Lf:
        return QByteArray("\n", 1);
    case LineEnding::CrLf:
        return QByteArray("\r\n", 2);
    }
    return {};
}

EncodeResult PayloadCodec::encodeHex(QString input) {
    EncodeResult result;
    input.remove(QRegularExpression(QStringLiteral("\\s+")));

    if (input.isEmpty()) {
        result.ok = true;
        return result;
    }

    if (input.size() % 2 != 0) {
        result.errorMessage = QStringLiteral("HEX 输入长度必须是偶数。请按完整字节输入，例如：01 03 00 00。");
        return result;
    }

    static const QRegularExpression hexPattern(QStringLiteral("^[0-9A-Fa-f]+$"));
    if (!hexPattern.match(input).hasMatch()) {
        result.errorMessage = QStringLiteral("HEX 输入只能包含 0-9、A-F、a-f 和空格。");
        return result;
    }

    result.payload = QByteArray::fromHex(input.toLatin1());
    result.ok = true;
    return result;
}

} // namespace svm::protocol
