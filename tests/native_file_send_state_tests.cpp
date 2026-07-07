#include "win32/native_file_send_state.h"
#include "transport/serial_write_queue.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

std::filesystem::path testFilePath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    for (std::uint8_t byte : bytes) {
        output.put(static_cast<char>(byte));
    }
}

void missingFileFailsBeforeActivation() {
    const std::filesystem::path path = testFilePath("svm-native-missing-file.bin");
    std::filesystem::remove(path);

    svm::win32::NativeFileSendState state;
    const auto result = state.open(path);
    assert(!result.ok());
    assert(result.status == svm::win32::NativeFileSendOpenStatus::FileSizeFailed);
    assert(!state.active());
    assert(state.totalBytes() == 0);
    assert(state.sentBytes() == 0);
}

void readsChunksAndTracksProgress() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-state.bin");
    writeBytes(path, {1, 2, 3, 4, 5});

    svm::win32::NativeFileSendState state;
    const auto open = state.open(path);
    assert(open.ok());
    assert(open.totalBytes == 5);
    assert(state.active());
    assert(state.progressPermille() == 0);

    auto chunk = state.readNextChunk(2);
    assert(chunk.ready());
    assert((chunk.bytes == std::vector<std::uint8_t>{1, 2}));
    state.markBytesWritten(chunk.bytes.size());
    assert(state.sentBytes() == 2);
    assert(state.progressPermille() == 400);
    assert(!state.done());

    chunk = state.readNextChunk(8);
    assert(chunk.ready());
    assert((chunk.bytes == std::vector<std::uint8_t>{3, 4, 5}));
    state.markBytesWritten(chunk.bytes.size());
    assert(state.sentBytes() == 5);
    assert(state.progressPermille() == 1000);
    assert(state.done());

    state.close();
    assert(!state.active());
    assert(state.totalBytes() == 5);
    assert(state.sentBytes() == 5);
    std::filesystem::remove(path);
}

void exactChunkCompletionUsesWrittenBytes() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-exact.bin");
    writeBytes(path, {9, 8, 7});

    svm::win32::NativeFileSendState state;
    assert(state.open(path).ok());
    auto chunk = state.readNextChunk(3);
    assert(chunk.ready());
    assert((chunk.bytes == std::vector<std::uint8_t>{9, 8, 7}));
    state.markBytesWritten(chunk.bytes.size());
    assert(state.done());

    std::filesystem::remove(path);
}

void finalChunkIsNotDoneUntilBytesAreMarkedWritten() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-final-chunk.bin");
    writeBytes(path, {6, 7, 8});

    svm::win32::NativeFileSendState state;
    assert(state.open(path).ok());
    auto chunk = state.readNextChunk(16);
    assert(chunk.ready());
    assert((chunk.bytes == std::vector<std::uint8_t>{6, 7, 8}));
    assert(!state.done());

    state.markBytesWritten(2);
    assert(state.sentBytes() == 2);
    assert(!state.done());

    state.markBytesWritten(1);
    assert(state.sentBytes() == 3);
    assert(state.done());

    std::filesystem::remove(path);
}

void queuedWriteDoesNotAdvanceProgressUntilSentResult() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-queued-write.bin");
    writeBytes(path, {1, 2, 3, 4});

    svm::win32::NativeFileSendState state;
    assert(state.open(path).ok());
    auto chunk = state.readNextChunk(2);
    assert(chunk.ready());

    svm::transport::SerialWriteQueue queue(1);
    const auto accepted = queue.enqueue(chunk.bytes);
    assert(accepted.status == svm::transport::SerialWriteResultStatus::Accepted);
    assert(state.sentBytes() == 0);
    assert(state.progressPermille() == 0);

    const auto sent = queue.completeNextSent(chunk.bytes.size());
    assert(sent.status == svm::transport::SerialWriteResultStatus::Sent);
    state.markBytesWritten(sent.byteCount);
    assert(state.sentBytes() == 2);
    assert(state.progressPermille() == 500);

    std::filesystem::remove(path);
}

void writtenBytesAreClampedToFileTotal() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-clamp.bin");
    writeBytes(path, {4, 5});

    svm::win32::NativeFileSendState state;
    assert(state.open(path).ok());
    state.markBytesWritten(1);
    assert(state.sentBytes() == 1);
    assert(state.progressPermille() == 500);

    state.markBytesWritten(9999);
    assert(state.sentBytes() == 2);
    assert(state.progressPermille() == 1000);
    assert(state.done());

    state.markBytesWritten(9999);
    assert(state.sentBytes() == 2);

    std::filesystem::remove(path);
}

void zeroByteFileCannotAccumulateProgress() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-empty.bin");
    writeBytes(path, {});

    svm::win32::NativeFileSendState state;
    assert(state.open(path).ok());
    assert(state.active());
    assert(state.totalBytes() == 0);
    assert(state.done());

    state.markBytesWritten(64);
    assert(state.sentBytes() == 0);
    assert(state.progressPermille() == 0);
    assert(state.done());

    std::filesystem::remove(path);
}

void invalidReadRequestsAreRejected() {
    const std::filesystem::path path = testFilePath("svm-native-file-send-invalid.bin");
    writeBytes(path, {1});

    svm::win32::NativeFileSendState state;
    assert(state.readNextChunk(1).status == svm::win32::NativeFileSendReadStatus::NotActive);
    assert(state.open(path).ok());
    assert(state.readNextChunk(0).status == svm::win32::NativeFileSendReadStatus::InvalidChunkSize);

    std::filesystem::remove(path);
}

} // namespace

int main() {
    missingFileFailsBeforeActivation();
    readsChunksAndTracksProgress();
    exactChunkCompletionUsesWrittenBytes();
    finalChunkIsNotDoneUntilBytesAreMarkedWritten();
    queuedWriteDoesNotAdvanceProgressUntilSentResult();
    writtenBytesAreClampedToFileTotal();
    zeroByteFileCannotAccumulateProgress();
    invalidReadRequestsAreRejected();

    std::cout << "native_file_send_state_tests passed\n";
    return 0;
}
