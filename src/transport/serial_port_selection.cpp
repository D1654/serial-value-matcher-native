#include "transport/serial_port_selection.h"

#include <optional>

namespace svm::transport {
namespace {

std::optional<QString> findPort(const QStringList& availablePortNames, const QString& desiredPort) {
    const QString normalizedDesired = desiredPort.trimmed();
    if (normalizedDesired.isEmpty()) {
        return std::nullopt;
    }

    for (const QString& portName : availablePortNames) {
        if (portName.compare(normalizedDesired, Qt::CaseInsensitive) == 0) {
            return portName;
        }
    }

    return std::nullopt;
}

QStringList normalizedAvailablePorts(const QStringList& portNames) {
    QStringList result;
    result.reserve(portNames.size());
    for (const QString& portName : portNames) {
        const QString trimmed = portName.trimmed();
        if (!trimmed.isEmpty()) {
            result.append(trimmed);
        }
    }
    return result;
}

} // namespace

bool SerialPortSelection::hasSelection() const {
    return !portName.trimmed().isEmpty();
}

SerialPortSelection SerialPortSelectionPolicy::choose(
    const QString& currentPort,
    const QString& profilePort,
    const QStringList& availablePortNames) {
    const QStringList available = normalizedAvailablePorts(availablePortNames);
    if (available.isEmpty()) {
        return {};
    }

    if (const auto selected = findPort(available, currentPort); selected.has_value()) {
        return {*selected, SerialPortSelectionReason::KeepCurrent};
    }

    if (const auto selected = findPort(available, profilePort); selected.has_value()) {
        return {*selected, SerialPortSelectionReason::RestoreProfile};
    }

    return {available.first(), SerialPortSelectionReason::FirstAvailable};
}

} // namespace svm::transport
