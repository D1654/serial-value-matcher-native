#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace svm::win32 {

inline constexpr std::size_t kNativeFileSendChunkBytes = 1024;

enum class NativeFileSendOpenStatus {
    Ready,
    FileSizeFailed,
    OpenFailed
};

struct NativeFileSendOpenResult {
    NativeFileSendOpenStatus status = NativeFileSendOpenStatus::Ready;
    std::uintmax_t totalBytes = 0;

    bool ok() const noexcept {
        return status == NativeFileSendOpenStatus::Ready;
    }
};

enum class NativeFileSendReadStatus {
    Ready,
    End,
    ReadFailed,
    InvalidChunkSize,
    NotActive
};

struct NativeFileSendChunk {
    NativeFileSendReadStatus status = NativeFileSendReadStatus::End;
    std::vector<std::uint8_t> bytes;

    bool ready() const noexcept {
        return status == NativeFileSendReadStatus::Ready;
    }
};

class NativeFileSendState final {
public:
    void setPath(std::filesystem::path path);
    const std::filesystem::path& path() const noexcept;

    NativeFileSendOpenResult open(std::filesystem::path path);
    void close();

    bool active() const noexcept;
    std::uintmax_t totalBytes() const noexcept;
    std::uintmax_t sentBytes() const noexcept;
    int progressPermille() const noexcept;

    NativeFileSendChunk readNextChunk(std::size_t chunkBytes);
    void markBytesWritten(std::size_t byteCount) noexcept;
    bool done() const noexcept;

private:
    std::filesystem::path path_;
    std::ifstream stream_;
    std::uintmax_t totalBytes_ = 0;
    std::uintmax_t sentBytes_ = 0;
    bool active_ = false;
};

} // namespace svm::win32
