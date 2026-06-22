#include "native_storage/native_store_record_io.h"

#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <ostream>

namespace svm::native_storage::store_io {
namespace {

bool parseUnsigned(const std::string& data, std::size_t& position, char delimiter, std::size_t& value) {
    const std::size_t delimiterPosition = data.find(delimiter, position);
    if (delimiterPosition == std::string::npos || delimiterPosition == position) {
        return false;
    }
    std::size_t parsed = 0;
    const char* begin = data.data() + position;
    const char* end = data.data() + delimiterPosition;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    value = parsed;
    position = delimiterPosition + 1;
    return true;
}

bool readStreamUnsigned(
    std::istream& input,
    char delimiter,
    std::size_t& value,
    std::string* errorText,
    std::string_view context) {
    std::string digits;
    char ch = '\0';
    while (input.get(ch)) {
        if (ch == delimiter) {
            if (digits.empty()) {
                if (errorText != nullptr) {
                    *errorText = std::string(context) + "失败：数字字段为空。";
                }
                return false;
            }
            const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
                if (errorText != nullptr) {
                    *errorText = std::string(context) + "失败：数字字段损坏。";
                }
                return false;
            }
            return true;
        }
        if (ch < '0' || ch > '9' || digits.size() >= 20) {
            if (errorText != nullptr) {
                *errorText = std::string(context) + "失败：记录边界损坏。";
            }
            return false;
        }
        digits.push_back(ch);
    }

    if (errorText != nullptr) {
        *errorText = std::string(context) + "失败：记录被截断。";
    }
    return false;
}

} // namespace

bool writeRecord(std::ostream& output, const Record& record) {
    output << record.size() << '|';
    for (const std::string& field : record) {
        output << field.size() << ':';
        output.write(field.data(), static_cast<std::streamsize>(field.size()));
        if (!output) {
            return false;
        }
    }
    output << '\n';
    return static_cast<bool>(output);
}

std::vector<Record> parseRecords(const std::string& data, std::string* errorText) {
    std::vector<Record> records;
    std::size_t position = data.rfind(std::string(kHeader), 0) == 0 ? kHeader.size() : 0;
    while (position < data.size()) {
        while (position < data.size() && (data[position] == '\n' || data[position] == '\r')) {
            ++position;
        }
        if (position >= data.size()) {
            break;
        }

        std::size_t fieldCount = 0;
        if (!parseUnsigned(data, position, '|', fieldCount)) {
            if (errorText != nullptr) {
                *errorText = "读取 native 存储失败：记录字段数量损坏。";
            }
            return {};
        }

        Record record;
        record.reserve(fieldCount);
        for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
            std::size_t fieldLength = 0;
            if (!parseUnsigned(data, position, ':', fieldLength) || position + fieldLength > data.size()) {
                if (errorText != nullptr) {
                    *errorText = "读取 native 存储失败：记录字段长度损坏。";
                }
                return {};
            }
            record.push_back(data.substr(position, fieldLength));
            position += fieldLength;
        }

        if (position < data.size() && data[position] == '\n') {
            ++position;
        }
        records.push_back(std::move(record));
    }
    return records;
}

bool skipStoreHeader(std::istream& input) {
    std::string prefix(kHeader.size(), '\0');
    input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    if (input.gcount() == static_cast<std::streamsize>(prefix.size()) && prefix == kHeader) {
        return true;
    }
    input.clear();
    input.seekg(0, std::ios::beg);
    return static_cast<bool>(input);
}

std::optional<RecordSpan> readNextRecordSpan(std::istream& input, std::string* errorText) {
    constexpr std::string_view context = "压缩 native 原始记录";
    while (true) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof()) {
            input.clear();
            return std::nullopt;
        }
        if (next != '\n' && next != '\r') {
            break;
        }
        input.get();
    }

    const std::streampos startPosition = input.tellg();
    if (startPosition == std::streampos(-1)) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：无法定位记录起点。";
        }
        return std::nullopt;
    }

    std::size_t fieldCount = 0;
    if (!readStreamUnsigned(input, '|', fieldCount, errorText, context)) {
        return std::nullopt;
    }

    for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
        std::size_t fieldLength = 0;
        if (!readStreamUnsigned(input, ':', fieldLength, errorText, context)) {
            return std::nullopt;
        }
        if (fieldLength > static_cast<std::size_t>(std::numeric_limits<std::streamoff>::max())) {
            if (errorText != nullptr) {
                *errorText = "压缩 native 原始记录失败：字段长度超出平台限制。";
            }
            return std::nullopt;
        }
        input.seekg(static_cast<std::streamoff>(fieldLength), std::ios::cur);
        if (!input) {
            if (errorText != nullptr) {
                *errorText = "压缩 native 原始记录失败：字段内容被截断。";
            }
            return std::nullopt;
        }
    }

    const std::streampos afterFields = input.tellg();
    if (afterFields == std::streampos(-1)) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：无法定位记录终点。";
        }
        return std::nullopt;
    }

    std::streampos endPosition = afterFields;
    const int next = input.peek();
    if (next == '\n') {
        input.get();
        endPosition = input.tellg();
    } else if (next == std::char_traits<char>::eof()) {
        input.clear();
    }

    if (endPosition == std::streampos(-1) || endPosition < startPosition) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：记录跨度损坏。";
        }
        return std::nullopt;
    }

    return RecordSpan{
        static_cast<std::uintmax_t>(startPosition),
        static_cast<std::uintmax_t>(endPosition - startPosition),
    };
}

std::optional<Record> readNextRecordFromStream(
    std::istream& input,
    std::string* errorText,
    std::string_view context) {
    while (true) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof()) {
            input.clear();
            return std::nullopt;
        }
        if (next != '\n' && next != '\r') {
            break;
        }
        input.get();
    }

    std::size_t fieldCount = 0;
    if (!readStreamUnsigned(input, '|', fieldCount, errorText, context)) {
        return std::nullopt;
    }

    Record record;
    record.reserve(fieldCount);
    for (std::size_t fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex) {
        std::size_t fieldLength = 0;
        if (!readStreamUnsigned(input, ':', fieldLength, errorText, context)) {
            return std::nullopt;
        }
        if (fieldLength > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            if (errorText != nullptr) {
                *errorText = std::string(context) + "失败：字段长度超出平台限制。";
            }
            return std::nullopt;
        }

        std::string field(fieldLength, '\0');
        if (fieldLength > 0) {
            input.read(field.data(), static_cast<std::streamsize>(fieldLength));
            if (input.gcount() != static_cast<std::streamsize>(fieldLength)) {
                if (errorText != nullptr) {
                    *errorText = std::string(context) + "失败：字段内容被截断。";
                }
                return std::nullopt;
            }
        }
        record.push_back(std::move(field));
    }

    const int next = input.peek();
    if (next == '\n') {
        input.get();
    } else if (next == std::char_traits<char>::eof()) {
        input.clear();
    }
    return record;
}

bool copyFileTailWithHeader(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& tempPath,
    std::uintmax_t offset,
    std::string* errorText) {
    std::ifstream input(sourcePath, std::ios::binary);
    if (!input) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：无法重新打开原始文件。";
        }
        return false;
    }
    if (offset > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：保留偏移超出平台限制。";
        }
        return false;
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：无法定位保留记录。";
        }
        return false;
    }

    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：无法创建临时文件。";
        }
        return false;
    }
    output << kHeader;

    std::array<char, 64 * 1024> buffer = {};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytesRead = input.gcount();
        if (bytesRead > 0) {
            output.write(buffer.data(), bytesRead);
            if (!output) {
                if (errorText != nullptr) {
                    *errorText = "压缩 native 原始记录失败：临时文件写入中断。";
                }
                return false;
            }
        }
    }
    if (input.bad()) {
        if (errorText != nullptr) {
            *errorText = "压缩 native 原始记录失败：读取原始文件中断。";
        }
        return false;
    }
    return true;
}

} // namespace svm::native_storage::store_io
