#pragma once

#include <QObject>

#include "capture/raw_io_event.h"
#include "capture/session_evidence.h"

namespace svm::capture {

class CaptureBus final : public QObject {
    Q_OBJECT

public:
    explicit CaptureBus(QObject* parent = nullptr);
    quint64 nextEvidenceOrder() const noexcept;

public slots:
    void publish(const RawIoEvent& event);

signals:
    void evidenceCaptured(const svm::capture::SessionEvidenceEvent& evidence);
    void eventCaptured(const svm::capture::RawIoEvent& event);

private:
    SessionEvidenceSequencer m_evidenceSequencer;
};

} // namespace svm::capture
