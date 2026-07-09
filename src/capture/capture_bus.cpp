#include "capture/capture_bus.h"

#include <QMetaType>

namespace svm::capture {

CaptureBus::CaptureBus(QObject* parent) : QObject(parent) {
    qRegisterMetaType<svm::capture::RawIoEvent>("svm::capture::RawIoEvent");
    qRegisterMetaType<svm::capture::SessionEvidenceEvent>("svm::capture::SessionEvidenceEvent");
}

quint64 CaptureBus::nextEvidenceOrder() const noexcept {
    return m_evidenceSequencer.nextOrder();
}

void CaptureBus::publish(const RawIoEvent& event) {
    const SessionEvidenceEvent evidence = m_evidenceSequencer.nextRawIoEvent(event);
    emit evidenceCaptured(evidence);
    emit eventCaptured(event);
}

} // namespace svm::capture
