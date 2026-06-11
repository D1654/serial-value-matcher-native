#include "transport/serial_port_enumerator.h"

#include <QSerialPortInfo>

namespace svm::transport {

QList<SerialPortDescriptor> SerialPortEnumerator::availablePorts() {
    QList<SerialPortDescriptor> descriptors;
    const auto ports = QSerialPortInfo::availablePorts();
    descriptors.reserve(ports.size());

    for (const auto& port : ports) {
        SerialPortDescriptor descriptor;
        descriptor.portName = port.portName();
        descriptor.systemLocation = port.systemLocation();
        descriptor.description = port.description();
        descriptor.manufacturer = port.manufacturer();
        descriptor.serialNumber = port.serialNumber();
        descriptor.hasVendorIdentifier = port.hasVendorIdentifier();
        descriptor.vendorIdentifier = descriptor.hasVendorIdentifier ? port.vendorIdentifier() : 0;
        descriptor.hasProductIdentifier = port.hasProductIdentifier();
        descriptor.productIdentifier = descriptor.hasProductIdentifier ? port.productIdentifier() : 0;
        descriptors.append(descriptor);
    }

    return descriptors;
}

} // namespace svm::transport
