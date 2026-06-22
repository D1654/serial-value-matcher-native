#include "win32/native_send_codec.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using svm::win32::NativeSendCodecErrors;
using svm::win32::NativeSendPayloadOptions;
using svm::win32::kNativeCodePageAscii;
using svm::win32::kNativeCodePageUtf8;

NativeSendCodecErrors errors() {
    NativeSendCodecErrors result;
    result.hexInvalidChar = L"hex invalid";
    result.hexOddNibble = L"hex odd";
    result.invalidDecimal = L"decimal invalid";
    result.invalidBinary = L"binary invalid";
    result.textEncodingFailed = L"encoding failed";
    return result;
}

void parsesHexWithSeparators() {
    std::wstring error;
    const auto payload = svm::win32::nativeParseHexPayload(L"01 03-0A,ff", errors(), &error);
    assert(error.empty());
    assert((payload == std::vector<std::uint8_t>{0x01, 0x03, 0x0A, 0xFF}));

    const auto bad = svm::win32::nativeParseHexPayload(L"0G", errors(), &error);
    assert(bad.empty());
    assert(error == L"hex invalid");

    const auto odd = svm::win32::nativeParseHexPayload(L"0", errors(), &error);
    assert(odd.empty());
    assert(error == L"hex odd");
}

void parsesDecimalAndBinaryStreams() {
    std::wstring error;
    const auto decimal = svm::win32::nativeParseDecimalPayload(L"1 2,255；0", errors(), &error);
    assert(error.empty());
    assert((decimal == std::vector<std::uint8_t>{1, 2, 255, 0}));

    const auto invalidDecimal = svm::win32::nativeParseDecimalPayload(L"256", errors(), &error);
    assert(invalidDecimal.empty());
    assert(error == L"decimal invalid");

    const auto binary = svm::win32::nativeParseBinaryPayload(L"00000001 11111111", errors(), &error);
    assert(error.empty());
    assert((binary == std::vector<std::uint8_t>{1, 255}));

    const auto invalidBinary = svm::win32::nativeParseBinaryPayload(L"101", errors(), &error);
    assert(invalidBinary.empty());
    assert(error == L"binary invalid");
}

void buildsPayloadWithLineEndings() {
    NativeSendPayloadOptions options;
    options.mode = 1;
    options.lineEnding = 3;
    const auto result = svm::win32::nativeBuildSendPayload(L"AA", options, errors());
    assert(result.errorText.empty());
    assert((result.payload == std::vector<std::uint8_t>{0xAA, '\r', '\n'}));
}

void encodesAsciiAndRejectsNonAscii() {
    std::wstring error;
    const auto ascii = svm::win32::nativeEncodeTextPayload(L"ABC", kNativeCodePageAscii, errors(), &error);
    assert(error.empty());
    assert((ascii == std::vector<std::uint8_t>{'A', 'B', 'C'}));

    const auto rejected = svm::win32::nativeEncodeTextPayload(L"\u6E29", kNativeCodePageAscii, errors(), &error);
    assert(rejected.empty());
    assert(error == L"encoding failed");
}

void formatsPayloadForLog() {
    const std::vector<std::uint8_t> payload = {0x01, 0x0A, 0xFF};
    assert(svm::win32::nativeBytesToHex(payload) == L"01 0A FF");
    assert(svm::win32::nativeBytesToDecimal(payload) == L"1 10 255");
    assert(svm::win32::nativeBytesToBinary({0x01, 0x80}) == L"00000001 10000000");
    assert(svm::win32::nativeDecodeBytesToText({'A', 0xFF}, kNativeCodePageAscii) == L"A.");
}

void utf8RoundTripOnWindows() {
    std::wstring error;
    const auto payload = svm::win32::nativeEncodeTextPayload(L"\u6E29\u5EA6", kNativeCodePageUtf8, errors(), &error);
    assert(error.empty());
    assert(svm::win32::nativeDecodeBytesToText(payload, kNativeCodePageUtf8) == L"\u6E29\u5EA6");
}

} // namespace

int main() {
    parsesHexWithSeparators();
    parsesDecimalAndBinaryStreams();
    buildsPayloadWithLineEndings();
    encodesAsciiAndRejectsNonAscii();
    formatsPayloadForLog();
#if defined(_WIN32)
    utf8RoundTripOnWindows();
#endif

    std::cout << "native_send_codec_tests passed\n";
    return 0;
}
