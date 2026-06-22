#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace svm::win32 {

inline constexpr unsigned int kNativeCodePageGbk = 936;
inline constexpr unsigned int kNativeCodePageAscii = 20127;
inline constexpr unsigned int kNativeCodePageUtf8 = 65001;

struct NativeSendCodecErrors {
    std::wstring hexInvalidChar;
    std::wstring hexOddNibble;
    std::wstring invalidDecimal;
    std::wstring invalidBinary;
    std::wstring textEncodingFailed;
};

struct NativeSendPayloadOptions {
    int mode = 0;
    unsigned int textCodePage = kNativeCodePageUtf8;
    int lineEnding = 0;
};

struct NativeSendPayloadResult {
    std::vector<std::uint8_t> payload;
    std::wstring errorText;
};

int nativeHexValue(wchar_t ch) noexcept;
std::vector<std::uint8_t> nativeParseHexPayload(std::wstring_view text, const NativeSendCodecErrors& errors, std::wstring* errorText);
std::vector<std::uint8_t> nativeParseDecimalPayload(std::wstring_view text, const NativeSendCodecErrors& errors, std::wstring* errorText);
std::vector<std::uint8_t> nativeParseBinaryPayload(std::wstring_view text, const NativeSendCodecErrors& errors, std::wstring* errorText);
std::vector<std::uint8_t> nativeEncodeTextPayload(std::wstring_view text, unsigned int codePage, const NativeSendCodecErrors& errors, std::wstring* errorText);
void nativeAppendLineEnding(std::vector<std::uint8_t>& payload, int lineEnding);
NativeSendPayloadResult nativeBuildSendPayload(std::wstring_view text, const NativeSendPayloadOptions& options, const NativeSendCodecErrors& errors);

std::wstring nativeBytesToHex(const std::vector<std::uint8_t>& bytes);
std::wstring nativeBytesToDecimal(const std::vector<std::uint8_t>& bytes);
std::wstring nativeBytesToBinary(const std::vector<std::uint8_t>& bytes);
std::wstring nativeDecodeBytesToText(const std::vector<std::uint8_t>& bytes, unsigned int codePage);

} // namespace svm::win32
