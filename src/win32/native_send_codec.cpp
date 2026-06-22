#include "win32/native_send_codec.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace svm::win32 {
namespace {

bool isByteTokenSeparator(wchar_t ch) noexcept {
    return std::iswspace(ch)
        || ch == L','
        || ch == L';'
        || ch == L'\uFF0C'
        || ch == L'\uFF1B'
        || ch == L'\u3001';
}

std::vector<std::wstring> splitByteTokens(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    for (wchar_t ch : text) {
        if (isByteTokenSeparator(ch)) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

void setError(std::wstring* errorText, const std::wstring& message) {
    if (errorText != nullptr) {
        *errorText = message;
    }
}

void clearError(std::wstring* errorText) {
    if (errorText != nullptr) {
        errorText->clear();
    }
}

std::wstring fallbackAsciiPreview(const std::vector<std::uint8_t>& bytes) {
    std::wstring text;
    text.reserve(bytes.size());
    for (std::uint8_t byte : bytes) {
        text.push_back(byte >= 0x20 && byte <= 0x7E ? static_cast<wchar_t>(byte) : L'.');
    }
    return text;
}

} // namespace

int nativeHexValue(wchar_t ch) noexcept {
    if (ch >= L'0' && ch <= L'9') {
        return ch - L'0';
    }
    if (ch >= L'a' && ch <= L'f') {
        return 10 + ch - L'a';
    }
    if (ch >= L'A' && ch <= L'F') {
        return 10 + ch - L'A';
    }
    return -1;
}

std::vector<std::uint8_t> nativeParseHexPayload(std::wstring_view text, const NativeSendCodecErrors& errors, std::wstring* errorText) {
    clearError(errorText);
    std::vector<int> nibbles;
    for (wchar_t ch : text) {
        if (std::iswspace(ch) || ch == L',' || ch == L'-') {
            continue;
        }
        const int value = nativeHexValue(ch);
        if (value < 0) {
            setError(errorText, errors.hexInvalidChar);
            return {};
        }
        nibbles.push_back(value);
    }

    if (nibbles.empty()) {
        return {};
    }
    if ((nibbles.size() % 2) != 0) {
        setError(errorText, errors.hexOddNibble);
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(nibbles.size() / 2);
    for (std::size_t index = 0; index < nibbles.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>((nibbles[index] << 4) | nibbles[index + 1]));
    }
    return bytes;
}

std::vector<std::uint8_t> nativeParseDecimalPayload(std::wstring_view text, const NativeSendCodecErrors& errors, std::wstring* errorText) {
    clearError(errorText);
    const std::vector<std::wstring> tokens = splitByteTokens(text);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(tokens.size());
    for (const std::wstring& token : tokens) {
        wchar_t* end = nullptr;
        const long value = std::wcstol(token.c_str(), &end, 10);
        if (end == token.c_str() || *end != L'\0' || value < 0 || value > 255) {
            setError(errorText, errors.invalidDecimal);
            return {};
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

std::vector<std::uint8_t> nativeParseBinaryPayload(std::wstring_view text, const NativeSendCodecErrors& errors, std::wstring* errorText) {
    clearError(errorText);
    std::wstring bits;
    for (wchar_t ch : text) {
        if (isByteTokenSeparator(ch)) {
            continue;
        }
        if (ch != L'0' && ch != L'1') {
            setError(errorText, errors.invalidBinary);
            return {};
        }
        bits.push_back(ch);
    }

    if (bits.empty()) {
        return {};
    }
    if ((bits.size() % 8) != 0) {
        setError(errorText, errors.invalidBinary);
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(bits.size() / 8);
    for (std::size_t index = 0; index < bits.size(); index += 8) {
        std::uint8_t value = 0;
        for (std::size_t bit = 0; bit < 8; ++bit) {
            value = static_cast<std::uint8_t>((value << 1U) | (bits[index + bit] == L'1' ? 1U : 0U));
        }
        bytes.push_back(value);
    }
    return bytes;
}

std::vector<std::uint8_t> nativeEncodeTextPayload(std::wstring_view text, unsigned int codePage, const NativeSendCodecErrors& errors, std::wstring* errorText) {
    clearError(errorText);
    if (text.empty()) {
        return {};
    }

    if (codePage == kNativeCodePageAscii) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(text.size());
        for (wchar_t ch : text) {
            if (ch > 0x7F) {
                setError(errorText, errors.textEncodingFailed);
                return {};
            }
            bytes.push_back(static_cast<std::uint8_t>(ch));
        }
        return bytes;
    }

#if defined(_WIN32)
    BOOL usedDefaultChar = FALSE;
    BOOL* usedDefaultCharPtr = codePage == CP_UTF8 ? nullptr : &usedDefaultChar;
    const int required = WideCharToMultiByte(
        codePage,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        usedDefaultCharPtr);
    if (required <= 0) {
        setError(errorText, errors.textEncodingFailed);
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(required));
    usedDefaultChar = FALSE;
    if (WideCharToMultiByte(
            codePage,
            0,
            text.data(),
            static_cast<int>(text.size()),
            reinterpret_cast<char*>(bytes.data()),
            required,
            nullptr,
            usedDefaultCharPtr) <= 0
        || usedDefaultChar) {
        setError(errorText, errors.textEncodingFailed);
        return {};
    }
    return bytes;
#else
    setError(errorText, errors.textEncodingFailed);
    return {};
#endif
}

void nativeAppendLineEnding(std::vector<std::uint8_t>& payload, int lineEnding) {
    switch (lineEnding) {
    case 1:
        payload.push_back('\r');
        break;
    case 2:
        payload.push_back('\n');
        break;
    case 3:
        payload.push_back('\r');
        payload.push_back('\n');
        break;
    default:
        break;
    }
}

NativeSendPayloadResult nativeBuildSendPayload(std::wstring_view text, const NativeSendPayloadOptions& options, const NativeSendCodecErrors& errors) {
    NativeSendPayloadResult result;
    if (options.mode == 1) {
        result.payload = nativeParseHexPayload(text, errors, &result.errorText);
    } else if (options.mode == 2) {
        result.payload = nativeParseDecimalPayload(text, errors, &result.errorText);
    } else if (options.mode == 3) {
        result.payload = nativeParseBinaryPayload(text, errors, &result.errorText);
    } else {
        result.payload = nativeEncodeTextPayload(text, options.textCodePage, errors, &result.errorText);
    }
    if (result.errorText.empty()) {
        nativeAppendLineEnding(result.payload, options.lineEnding);
    }
    return result;
}

std::wstring nativeBytesToHex(const std::vector<std::uint8_t>& bytes) {
    std::wostringstream output;
    output.setf(std::ios::uppercase);
    output << std::hex;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << L' ';
        }
        output.width(2);
        output.fill(L'0');
        output << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::wstring nativeBytesToDecimal(const std::vector<std::uint8_t>& bytes) {
    std::wostringstream output;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << L' ';
        }
        output << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::wstring nativeBytesToBinary(const std::vector<std::uint8_t>& bytes) {
    std::wostringstream output;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) {
            output << L' ';
        }
        for (int bit = 7; bit >= 0; --bit) {
            output << (((bytes[index] >> bit) & 1U) != 0 ? L'1' : L'0');
        }
    }
    return output.str();
}

std::wstring nativeDecodeBytesToText(const std::vector<std::uint8_t>& bytes, unsigned int codePage) {
    if (bytes.empty()) {
        return {};
    }

    if (codePage == kNativeCodePageAscii) {
        std::wstring text;
        text.reserve(bytes.size());
        for (std::uint8_t byte : bytes) {
            text.push_back(byte <= 0x7F ? static_cast<wchar_t>(byte) : L'.');
        }
        return text;
    }

#if defined(_WIN32)
    const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    const int required = MultiByteToWideChar(
        codePage,
        flags,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        nullptr,
        0);
    if (required <= 0) {
        return fallbackAsciiPreview(bytes);
    }

    std::wstring text(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        text.data(),
        required);
    return text;
#else
    return fallbackAsciiPreview(bytes);
#endif
}

} // namespace svm::win32
