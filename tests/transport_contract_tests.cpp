#include "transport/serial_session.h"
#include "transport/serial_transport.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeSerialSession final
    : public svm::transport::SerialSession,
      public svm::transport::SerialByteStream,
      public svm::transport::SerialWriteScheduler {
public:
    svm::transport::SerialOperationResult open(svm::transport::SerialOpenOptions options) override {
        options_ = std::move(options);
        ++generation_;
        state_ = svm::transport::SerialSessionState::Open;
        return result(
            svm::transport::SerialOperationKind::Open,
            svm::transport::SerialOperationStatus::Succeeded);
    }

    svm::transport::SerialOperationResult close() override {
        state_ = svm::transport::SerialSessionState::Closed;
        return result(
            svm::transport::SerialOperationKind::Close,
            svm::transport::SerialOperationStatus::Succeeded);
    }

    svm::transport::SerialSessionSnapshot snapshot() const override {
        return {
            .state = state_,
            .generation = generation_,
            .endpoint = options_.portName,
            .options = options_,
        };
    }

    svm::transport::SerialOperationResult setDataTerminalReady(bool enabled) override {
        options_.dataTerminalReady = enabled;
        return result(
            svm::transport::SerialOperationKind::SetDataTerminalReady,
            svm::transport::SerialOperationStatus::Succeeded);
    }

    svm::transport::SerialOperationResult setRequestToSend(bool enabled) override {
        options_.requestToSend = enabled;
        return result(
            svm::transport::SerialOperationKind::SetRequestToSend,
            svm::transport::SerialOperationStatus::Succeeded);
    }

    svm::transport::SerialByteStream& byteStream() noexcept override {
        return *this;
    }

    svm::transport::SerialWriteScheduler& writeScheduler() noexcept override {
        return *this;
    }

    svm::transport::SerialTerminalResult writeBytes(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline) override {
        received_ = std::move(payload);
        return result(
            svm::transport::SerialOperationKind::Write,
            svm::transport::SerialOperationStatus::Succeeded,
            deadline,
            received_.size());
    }

    svm::transport::SerialReadResult readAvailable(
        std::size_t maxBytes,
        svm::transport::SerialDeadline deadline) override {
        const std::size_t count = std::min(maxBytes, received_.size());
        std::vector<std::uint8_t> bytes(
            received_.begin(),
            received_.begin() + static_cast<std::ptrdiff_t>(count));
        received_.erase(
            received_.begin(),
            received_.begin() + static_cast<std::ptrdiff_t>(count));
        return {
            .operation = result(
                svm::transport::SerialOperationKind::Read,
                svm::transport::SerialOperationStatus::Succeeded,
                deadline,
                count),
            .bytes = std::move(bytes),
        };
    }

    svm::transport::SerialWriteAdmissionResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        svm::transport::SerialDeadline deadline) override {
        return result(
            svm::transport::SerialOperationKind::Write,
            svm::transport::SerialOperationStatus::Accepted,
            deadline,
            payload.size());
    }

    std::vector<svm::transport::SerialTerminalResult> cancelPendingWrites() override {
        return {};
    }

    std::vector<svm::transport::SerialTerminalResult> takeCompletedWrites() override {
        return {};
    }

    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override {
        return {};
    }

private:
    svm::transport::SerialOperationResult result(
        svm::transport::SerialOperationKind kind,
        svm::transport::SerialOperationStatus status,
        svm::transport::SerialDeadline deadline = {},
        std::size_t byteCount = 0) {
        return {
            .operation = {
                .requestId = nextRequestId_++,
                .generation = generation_,
                .kind = kind,
                .deadline = deadline,
            },
            .status = status,
            .deadlineStatus = deadline.set()
                ? svm::transport::SerialDeadlineStatus::Met
                : svm::transport::SerialDeadlineStatus::NotSet,
            .byteCount = byteCount,
            .endpoint = options_.portName,
        };
    }

    svm::transport::SerialOpenOptions options_;
    std::vector<std::uint8_t> received_;
    svm::transport::SerialSessionState state_ = svm::transport::SerialSessionState::Closed;
    svm::transport::SerialSessionGeneration generation_ = svm::transport::kUnassignedSerialSessionGeneration;
    svm::transport::SerialOperationId nextRequestId_ = 1;
};

class FakeSerialTransport final : public svm::transport::SerialTransport {
public:
    bool open(svm::transport::SerialOpenOptions options) override {
        options_ = std::move(options);
        open_ = !options_.portName.empty();
        error_.clear();
        return open_;
    }

    void close() override {
        open_ = false;
        queue_.clear();
        completed_.clear();
    }

    bool isOpen() const noexcept override { return open_; }
    std::string endpoint() const override { return options_.portName; }
    std::string lastErrorText() const override { return error_; }
    bool usesHardwareRtsCts() const noexcept override {
        return options_.flowControl == svm::transport::SerialFlowControl::HardwareRtsCts;
    }

    bool setDataTerminalReady(bool enabled) override {
        options_.dataTerminalReady = enabled;
        return open_;
    }

    bool setRequestToSend(bool enabled) override {
        options_.requestToSend = enabled;
        return open_;
    }

    svm::transport::SerialIoResult writeBytes(const std::vector<std::uint8_t>& payload) override {
        if (!open_) {
            error_ = "closed";
            return {false, 0, error_};
        }
        received_ = payload;
        return {true, payload.size(), {}};
    }

    svm::transport::SerialWriteResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        std::optional<int> timeoutMs = std::nullopt) override {
        return queue_.enqueue(std::move(payload), timeoutMs.value_or(svm::transport::kDefaultSerialWriteTimeoutMs));
    }

    std::vector<svm::transport::SerialWriteResult> cancelPendingWrites() override {
        return queue_.cancelAllPending();
    }

    std::vector<svm::transport::SerialWriteResult> takeCompletedWrites() override {
        std::vector<svm::transport::SerialWriteResult> result;
        result.swap(completed_);
        return result;
    }

    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override {
        return queue_.snapshot();
    }

    bool waitForReadyRead(int) override { return !received_.empty(); }

    std::vector<std::uint8_t> readAvailable(std::size_t maxBytes) override {
        const std::size_t count = std::min(maxBytes, received_.size());
        std::vector<std::uint8_t> result(received_.begin(), received_.begin() + static_cast<std::ptrdiff_t>(count));
        received_.erase(received_.begin(), received_.begin() + static_cast<std::ptrdiff_t>(count));
        return result;
    }

private:
    svm::transport::SerialOpenOptions options_;
    svm::transport::SerialWriteQueue queue_;
    std::vector<svm::transport::SerialWriteResult> completed_;
    std::vector<std::uint8_t> received_;
    std::string error_;
    bool open_ = false;
};

} // namespace

int main() {
    FakeSerialSession session;
    svm::transport::SerialOpenOptions sessionOptions;
    sessionOptions.portName = "COM5";
    sessionOptions.flowControl = svm::transport::SerialFlowControl::HardwareRtsCts;

    const auto opened = session.open(sessionOptions);
    assert(opened.succeeded());
    assert(opened.operation.assigned());
    assert(opened.operation.generation == 1);
    assert(session.snapshot().open());
    assert(session.snapshot().usesHardwareRtsCts());
    assert(std::string(svm::transport::serialSessionStateName(session.snapshot().state)) == "open");

    const std::vector<std::uint8_t> sessionPayload{4, 5, 6};
    const auto sessionWrite = session.byteStream().writeBytes(sessionPayload);
    assert(sessionWrite.terminal());
    assert(sessionWrite.byteCount == sessionPayload.size());
    const auto sessionRead = session.byteStream().readAvailable(sessionPayload.size());
    assert(sessionRead.operation.succeeded());
    assert(sessionRead.bytes == sessionPayload);

    const auto admission = session.writeScheduler().enqueueWrite(sessionPayload);
    assert(admission.accepted());
    assert(admission.operation.generation == session.snapshot().generation);

    const auto closed = session.close();
    assert(closed.succeeded());
    assert(session.snapshot().state == svm::transport::SerialSessionState::Closed);

    FakeSerialTransport transport;
    svm::transport::SerialOpenOptions options;
    options.portName = "COM7";
    options.flowControl = svm::transport::SerialFlowControl::HardwareRtsCts;

    assert(transport.open(options));
    assert(transport.isOpen());
    assert(transport.endpoint() == "COM7");
    assert(transport.usesHardwareRtsCts());

    const std::vector<std::uint8_t> payload{1, 2, 3};
    const auto write = transport.writeBytes(payload);
    assert(write.ok && write.byteCount == payload.size());
    assert(transport.waitForReadyRead(0));
    assert(transport.readAvailable(2) == std::vector<std::uint8_t>({1, 2}));
    assert(transport.readAvailable(2) == std::vector<std::uint8_t>({3}));

    const auto queued = transport.enqueueWrite(payload);
    assert(queued.accepted());
    assert(transport.writeQueueSnapshot().pendingCount == 1);
    const auto cancelled = transport.cancelPendingWrites();
    assert(cancelled.size() == 1);
    assert(cancelled.front().status == svm::transport::SerialWriteResultStatus::Cancelled);
    assert(transport.writeQueueSnapshot().empty());

    transport.close();
    assert(!transport.isOpen());
    return 0;
}
