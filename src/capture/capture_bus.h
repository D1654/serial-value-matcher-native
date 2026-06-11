#pragma once

#include <QObject>

#include "capture/raw_io_event.h"

namespace svm::capture {

class CaptureBus final : public QObject {
    Q_OBJECT

public:
    explicit CaptureBus(QObject* parent = nullptr);

public slots:
    void publish(const RawIoEvent& event);

signals:
    void eventCaptured(const svm::capture::RawIoEvent& event);
};

} // namespace svm::capture
