#pragma once

#if defined(_WIN32)

#include <string>
#include <string_view>

namespace svm::win32 {

std::wstring utf8ToWide(std::string_view value);
std::string wideToUtf8(std::wstring_view value);

} // namespace svm::win32

#endif
