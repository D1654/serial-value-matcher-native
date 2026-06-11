#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace svm::protocol {

quint8 sum8(const QByteArray& data);
quint8 xor8(const QByteArray& data);
quint8 lrc8(const QByteArray& data);

} // namespace svm::protocol
