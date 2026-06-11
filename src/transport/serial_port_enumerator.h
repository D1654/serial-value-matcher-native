#pragma once

#include <QList>

#include "transport/serial_port_descriptor.h"

namespace svm::transport {

class SerialPortEnumerator final {
public:
    static QList<SerialPortDescriptor> availablePorts();
};

} // namespace svm::transport
