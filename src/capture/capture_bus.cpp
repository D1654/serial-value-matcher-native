#include "capture/capture_bus.h"

#include <QMetaType>

namespace svm::capture {

CaptureBus::CaptureBus(QObject* parent) : QObject(parent) {
    qRegisterMetaType<svm::capture::RawIoEvent>("svm::capture::RawIoEvent");
}

void CaptureBus::publish(const RawIoEvent& event) {
    emit eventCaptured(event);
}

} // namespace svm::capture
