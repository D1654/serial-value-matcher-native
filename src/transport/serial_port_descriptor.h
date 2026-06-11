#pragma once

#include <QString>

namespace svm::transport {

struct SerialPortDescriptor {
    QString portName;
    QString systemLocation;
    QString description;
    QString manufacturer;
    QString serialNumber;
    bool hasVendorIdentifier = false;
    quint16 vendorIdentifier = 0;
    bool hasProductIdentifier = false;
    quint16 productIdentifier = 0;
};

} // namespace svm::transport
