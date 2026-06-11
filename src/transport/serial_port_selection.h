#pragma once

#include <QString>
#include <QStringList>

namespace svm::transport {

enum class SerialPortSelectionReason {
    KeepCurrent,
    RestoreProfile,
    FirstAvailable,
    NoneAvailable
};

struct SerialPortSelection {
    QString portName;
    SerialPortSelectionReason reason = SerialPortSelectionReason::NoneAvailable;

    [[nodiscard]] bool hasSelection() const;
};

class SerialPortSelectionPolicy final {
public:
    static SerialPortSelection choose(
        const QString& currentPort,
        const QString& profilePort,
        const QStringList& availablePortNames);
};

} // namespace svm::transport
