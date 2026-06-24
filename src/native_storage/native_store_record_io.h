#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace svm::native_storage::store_io {

using Record = std::vector<std::string>;

inline constexpr std::string_view kHeader = "SVM_NATIVE_STORE_V1\n";

struct RecordSpan {
    std::uintmax_t offset = 0;
    std::uintmax_t size = 0;
};

bool writeRecord(std::ostream& output, const Record& record);
std::vector<Record> parseRecords(const std::string& data, std::string* errorText);
bool skipStoreHeader(std::istream& input);
std::optional<RecordSpan> readNextRecordSpan(
    std::istream& input,
    std::string* errorText,
    std::string_view context);
std::optional<Record> readNextRecordFromStream(
    std::istream& input,
    std::string* errorText,
    std::string_view context);
bool copyFileTailWithHeader(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& tempPath,
    std::uintmax_t offset,
    std::string* errorText);

} // namespace svm::native_storage::store_io
