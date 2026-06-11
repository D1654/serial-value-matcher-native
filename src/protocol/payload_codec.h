#pragma once

#include <QByteArray>
#include <QString>

namespace svm::protocol {

enum class PayloadMode {
    Text,
    Hex
};

enum class LineEnding {
    None,
    Cr,
    Lf,
    CrLf
};

struct EncodeResult {
    bool ok = false;
    QByteArray payload;
    QString errorMessage;
};

class PayloadCodec final {
public:
    static EncodeResult encode(QString input, PayloadMode mode, LineEnding lineEnding);
    static QString bytesToHex(const QByteArray& payload);
    static QByteArray lineEndingBytes(LineEnding lineEnding);

private:
    static EncodeResult encodeHex(QString input);
};

} // namespace svm::protocol
