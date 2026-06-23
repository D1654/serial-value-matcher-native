#pragma once

#if defined(_WIN32)

#include <string>

namespace svm::win32 {

std::wstring nativeLocalClockText();
std::string nativeUtcTimestampText();
std::string nativeTimestampIdText();

} // namespace svm::win32

#endif
