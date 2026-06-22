#include "win32/native_file_send_state.h"

#include <algorithm>
#include <utility>

namespace svm::win32 {

void NativeFileSendState::setPath(std::filesystem::path path) {
    path_ = std::move(path);
}

const std::filesystem::path& NativeFileSendState::path() const noexcept {
    return path_;
}

NativeFileSendOpenResult NativeFileSendState::open(std::filesystem::path path) {
    close();
    path_ = std::move(path);

    std::error_code error;
    totalBytes_ = std::filesystem::file_size(path_, error);
    sentBytes_ = 0;
    if (error) {
        totalBytes_ = 0;
        return {NativeFileSendOpenStatus::FileSizeFailed, 0};
    }

    stream_.open(path_, std::ios::binary);
    if (!stream_) {
        totalBytes_ = 0;
        return {NativeFileSendOpenStatus::OpenFailed, 0};
    }

    active_ = true;
    return {NativeFileSendOpenStatus::Ready, totalBytes_};
}

void NativeFileSendState::close() {
    if (stream_.is_open()) {
        stream_.close();
    }
    stream_.clear();
    active_ = false;
}

bool NativeFileSendState::active() const noexcept {
    return active_;
}

std::uintmax_t NativeFileSendState::totalBytes() const noexcept {
    return totalBytes_;
}

std::uintmax_t NativeFileSendState::sentBytes() const noexcept {
    return sentBytes_;
}

int NativeFileSendState::progressPermille() const noexcept {
    if (totalBytes_ == 0) {
        return 0;
    }
    return static_cast<int>(std::min<std::uintmax_t>(1000, (sentBytes_ * 1000) / totalBytes_));
}

NativeFileSendChunk NativeFileSendState::readNextChunk(std::size_t chunkBytes) {
    if (!active_) {
        return {NativeFileSendReadStatus::NotActive, {}};
    }
    if (chunkBytes == 0) {
        return {NativeFileSendReadStatus::InvalidChunkSize, {}};
    }

    std::vector<std::uint8_t> chunk(chunkBytes);
    stream_.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize readCount = stream_.gcount();
    if (stream_.bad()) {
        return {NativeFileSendReadStatus::ReadFailed, {}};
    }
    if (readCount <= 0) {
        return {NativeFileSendReadStatus::End, {}};
    }

    chunk.resize(static_cast<std::size_t>(readCount));
    return {NativeFileSendReadStatus::Ready, std::move(chunk)};
}

void NativeFileSendState::markBytesWritten(std::size_t byteCount) noexcept {
    const std::uintmax_t next = sentBytes_ + static_cast<std::uintmax_t>(byteCount);
    sentBytes_ = totalBytes_ == 0 ? next : std::min<std::uintmax_t>(next, totalBytes_);
}

bool NativeFileSendState::done() const noexcept {
    return active_ && (sentBytes_ >= totalBytes_ || stream_.eof());
}

} // namespace svm::win32
