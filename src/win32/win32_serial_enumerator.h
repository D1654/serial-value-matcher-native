#pragma once

#include "win32/win32_serial_types.h"

#if defined(_WIN32)

#include <vector>

namespace svm::win32 {

class Win32SerialEnumerator final {
public:
    static std::vector<SerialPortDescriptor> availablePorts();
};

} // namespace svm::win32

#endif
