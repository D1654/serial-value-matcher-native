#include "transport/serial_session.h"
#include "transport/serial_write_queue.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using svm::transport::SerialDeadline;
using svm::transport::SerialDeadlineStatus;
using svm::transport::SerialDataDirection;
using svm::transport::SerialErrorCategory;
using svm::transport::SerialOperationKind;
using svm::transport::SerialOperationResult;
using svm::transport::SerialOperationStatus;
using svm::transport::SerialReadResult;
using svm::transport::SerialSessionGeneration;
using svm::transport::SerialSessionSnapshot;
using svm::transport::SerialSessionState;
using svm::transport::SerialTerminalResult;
using svm::transport::SerialWriteQueue;
using svm::transport::SerialWriteQueueLimits;
using svm::transport::SerialWriteRequest;
using svm::transport::SerialWriteResult;
using svm::transport::SerialWriteResultStatus;

template <typename Result>
concept ContainsPayload = requires(const Result& result) {
    result.payload;
};

template <typename Result>
concept ContainsLocalizedMessage = requires(const Result& result) {
    result.message;
};

static_assert(!ContainsPayload<SerialOperationResult>);
static_assert(!ContainsLocalizedMessage<SerialOperationResult>);
static_assert(!ContainsLocalizedMessage<SerialWriteResult>);

SerialDeadline deadlineAt(std::int64_t milliseconds) {
    return {
        .expiresAt = std::chrono::steady_clock::time_point(
            std::chrono::milliseconds(milliseconds)),
    };
}

class FakeSerialSession final
    : public svm::transport::SerialSession,
      public svm::transport::SerialByteStream,
      public svm::transport::SerialWriteScheduler {
public:
    explicit FakeSerialSession(
        SerialWriteQueueLimits limits = {},
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point(
            std::chrono::milliseconds(1000)))
        : queue_(limits), now_(now) {
        recordState();
    }

    SerialOperationResult open(svm::transport::SerialOpenOptions options) override {
        if (state_ != SerialSessionState::Closed) {
            return rejected(
                SerialOperationKind::Open,
                SerialOperationStatus::RejectedInvalid,
                SerialErrorCategory::InvalidInput);
        }

        state_ = SerialSessionState::Opening;
        recordState();
        if (options.portName.empty()) {
            state_ = SerialSessionState::Faulted;
            recordState();
            return assigned(
                SerialOperationKind::Open,
                SerialOperationStatus::Failed,
                {},
                0,
                SerialErrorCategory::InvalidInput,
                0,
                svm::transport::kUnassignedSerialSessionGeneration,
                {});
        }

        ++generationCounter_;
        generation_ = generationCounter_;
        const bool generationStarted = queue_.beginGeneration(generation_);
        assert(generationStarted);
        options_ = std::move(options);
        state_ = SerialSessionState::Open;
        recordState();
        return assigned(
            SerialOperationKind::Open,
            SerialOperationStatus::Succeeded,
            {},
            0,
            SerialErrorCategory::None,
            0,
            generation_,
            options_.portName);
    }

    SerialOperationResult close() override {
        if (state_ != SerialSessionState::Open) {
            return rejected(
                SerialOperationKind::Close,
                SerialOperationStatus::RejectedClosed,
                SerialErrorCategory::SessionClosed);
        }

        const SerialSessionGeneration closingGeneration = generation_;
        const std::string closingEndpoint = options_.portName;
        state_ = SerialSessionState::Closing;
        recordState();
        settleForClose(closingEndpoint);
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
        options_ = {};
        state_ = SerialSessionState::Closed;
        recordState();
        return assigned(
            SerialOperationKind::Close,
            SerialOperationStatus::Succeeded,
            {},
            0,
            SerialErrorCategory::None,
            0,
            closingGeneration,
            closingEndpoint);
    }

    SerialSessionSnapshot snapshot() const override {
        return {
            .state = state_,
            .generation = generation_,
            .endpoint = options_.portName,
            .options = options_,
        };
    }

    SerialOperationResult setDataTerminalReady(bool enabled) override {
        if (state_ != SerialSessionState::Open) {
            return rejectedClosed(SerialOperationKind::SetDataTerminalReady);
        }
        options_.dataTerminalReady = enabled;
        return assigned(
            SerialOperationKind::SetDataTerminalReady,
            SerialOperationStatus::Succeeded);
    }

    SerialOperationResult setRequestToSend(bool enabled) override {
        if (state_ != SerialSessionState::Open) {
            return rejectedClosed(SerialOperationKind::SetRequestToSend);
        }
        options_.requestToSend = enabled;
        return assigned(
            SerialOperationKind::SetRequestToSend,
            SerialOperationStatus::Succeeded);
    }

    svm::transport::SerialByteStream& byteStream() noexcept override {
        return *this;
    }

    svm::transport::SerialWriteScheduler& writeScheduler() noexcept override {
        return *this;
    }

    SerialTerminalResult writeBytes(
        std::vector<std::uint8_t> payload,
        SerialDeadline deadline) override {
        if (state_ != SerialSessionState::Open) {
            return rejectedClosed(SerialOperationKind::Write, deadline);
        }
        if (payload.empty()) {
            return rejected(
                SerialOperationKind::Write,
                SerialOperationStatus::RejectedInvalid,
                SerialErrorCategory::InvalidInput,
                deadline);
        }
        if (expired(deadline)) {
            return assigned(
                SerialOperationKind::Write,
                SerialOperationStatus::Timeout,
                deadline,
                0,
                SerialErrorCategory::Timeout);
        }

        const ScriptedOutcome outcome = nextWrite_.value_or(ScriptedOutcome{
            .status = SerialOperationStatus::Succeeded,
            .category = SerialErrorCategory::None,
            .byteCount = payload.size(),
        });
        nextWrite_.reset();
        if (outcome.status == SerialOperationStatus::Succeeded) {
            received_ = payload;
        }
        return assigned(
            SerialOperationKind::Write,
            outcome.status,
            deadline,
            outcome.byteCount,
            outcome.category,
            outcome.nativeCode);
    }

    SerialReadResult readAvailable(
        std::size_t maxBytes,
        SerialDeadline deadline) override {
        if (state_ != SerialSessionState::Open) {
            return {.operation = rejectedClosed(SerialOperationKind::Read, deadline)};
        }
        if (maxBytes == 0) {
            return {
                .operation = rejected(
                    SerialOperationKind::Read,
                    SerialOperationStatus::RejectedInvalid,
                    SerialErrorCategory::InvalidInput,
                    deadline),
            };
        }
        if (expired(deadline)) {
            return {
                .operation = assigned(
                    SerialOperationKind::Read,
                    SerialOperationStatus::Timeout,
                    deadline,
                    0,
                    SerialErrorCategory::Timeout),
            };
        }
        if (nextRead_.has_value()) {
            const ScriptedOutcome outcome = *nextRead_;
            nextRead_.reset();
            return {
                .operation = assigned(
                    SerialOperationKind::Read,
                    outcome.status,
                    deadline,
                    outcome.byteCount,
                    outcome.category,
                    outcome.nativeCode),
            };
        }

        const std::size_t count = std::min(maxBytes, received_.size());
        std::vector<std::uint8_t> bytes(
            received_.begin(),
            received_.begin() + static_cast<std::ptrdiff_t>(count));
        received_.erase(
            received_.begin(),
            received_.begin() + static_cast<std::ptrdiff_t>(count));
        return {
            .operation = assigned(
                SerialOperationKind::Read,
                SerialOperationStatus::Succeeded,
                deadline,
                count),
            .bytes = std::move(bytes),
        };
    }

    svm::transport::SerialWriteAdmissionResult enqueueWrite(
        std::vector<std::uint8_t> payload,
        SerialDeadline deadline) override {
        if (state_ != SerialSessionState::Open) {
            return rejectedClosed(SerialOperationKind::Write, deadline);
        }
        return admission(queue_.enqueue(
            std::move(payload),
            svm::transport::kDefaultSerialWriteTimeoutMs,
            generation_,
            deadline));
    }

    std::vector<SerialTerminalResult> cancelPendingWrites() override {
        std::vector<SerialTerminalResult> cancelled;
        for (const SerialWriteResult& result : queue_.cancelAllPending()) {
            cancelled.push_back(queueTerminal(
                result,
                options_.portName,
                SerialOperationStatus::Cancelled,
                SerialErrorCategory::Cancelled));
            evidence_.push_back(cancelled.back());
        }
        return cancelled;
    }

    std::vector<SerialTerminalResult> takeCompletedWrites() override {
        std::vector<SerialTerminalResult> results;
        results.swap(completed_);
        return results;
    }

    svm::transport::SerialWriteQueueSnapshot writeQueueSnapshot() const override {
        return queue_.snapshot();
    }

    std::optional<SerialWriteRequest> activateNextWriteForTest() {
        std::optional<SerialWriteRequest> activated = queue_.activateNext();
        if (activated.has_value()) {
            active_ = activated;
        }
        return activated;
    }

    SerialOperationResult deliverCompletionForTest(
        svm::transport::SerialOperationId requestId,
        SerialSessionGeneration generation,
        SerialOperationStatus status,
        std::size_t byteCount = 0,
        SerialErrorCategory category = SerialErrorCategory::None,
        std::uint32_t nativeCode = 0) {
        if (!active_.has_value()
            || active_->id != requestId
            || active_->generation != generation) {
            return rejectedCompletion(requestId, generation);
        }
        if (status == SerialOperationStatus::Succeeded && expired(active_->deadline)) {
            status = SerialOperationStatus::Timeout;
            byteCount = 0;
            category = SerialErrorCategory::Timeout;
            nativeCode = 0;
        }

        const std::optional<SerialWriteResultStatus> queueStatus = queueTerminalStatus(status);
        if (!queueStatus.has_value()) {
            return rejectedCompletion(requestId, generation);
        }
        const SerialWriteResult queueResult = queue_.completeActive(
            requestId,
            generation,
            *queueStatus,
            byteCount);
        if (queueResult.rejected()) {
            return rejectedCompletion(requestId, generation);
        }

        const SerialOperationStatus terminalStatus = operationStatus(queueResult.status);
        SerialTerminalResult terminal = queueTerminal(
            queueResult,
            options_.portName,
            terminalStatus,
            category == SerialErrorCategory::None
                ? defaultErrorCategory(terminalStatus)
                : category,
            nativeCode);
        active_.reset();
        completed_.push_back(terminal);
        evidence_.push_back(terminal);
        return terminal;
    }

    SerialOperationResult cancelActiveWriteForTest() {
        if (!active_.has_value()) {
            return rejectedCompletion(0, generation_);
        }
        return deliverCompletionForTest(
            active_->id,
            active_->generation,
            SerialOperationStatus::Cancelled);
    }

    void scriptNextWrite(
        SerialOperationStatus status,
        SerialErrorCategory category,
        std::size_t byteCount,
        std::uint32_t nativeCode = 0) {
        nextWrite_ = ScriptedOutcome{
            .status = status,
            .category = category,
            .byteCount = byteCount,
            .nativeCode = nativeCode,
        };
    }

    void scriptNextRead(
        SerialOperationStatus status,
        SerialErrorCategory category,
        std::size_t byteCount = 0,
        std::uint32_t nativeCode = 0) {
        nextRead_ = ScriptedOutcome{
            .status = status,
            .category = category,
            .byteCount = byteCount,
            .nativeCode = nativeCode,
        };
    }

    void setNowForTest(std::chrono::steady_clock::time_point now) noexcept {
        now_ = now;
    }

    void setNonOpenStateForTest(SerialSessionState state) {
        assert(state != SerialSessionState::Open);
        state_ = state;
        generation_ = svm::transport::kUnassignedSerialSessionGeneration;
        options_ = {};
        recordState();
    }

    const std::vector<SerialSessionSnapshot>& stateHistory() const noexcept {
        return stateHistory_;
    }

    const std::vector<SerialTerminalResult>& evidence() const noexcept {
        return evidence_;
    }

    std::size_t completionCount() const noexcept {
        return completed_.size();
    }

private:
    struct ScriptedOutcome {
        SerialOperationStatus status = SerialOperationStatus::Succeeded;
        SerialErrorCategory category = SerialErrorCategory::None;
        std::size_t byteCount = 0;
        std::uint32_t nativeCode = 0;
    };

    bool expired(const SerialDeadline& deadline) const noexcept {
        return deadline.expiresAt.has_value() && *deadline.expiresAt <= now_;
    }

    SerialDeadlineStatus deadlineStatus(
        SerialOperationStatus status,
        const SerialDeadline& deadline) const noexcept {
        if (!deadline.set()) {
            return SerialDeadlineStatus::NotSet;
        }
        if (status == SerialOperationStatus::Accepted
            || status == SerialOperationStatus::RejectedInvalid
            || status == SerialOperationStatus::RejectedFull
            || status == SerialOperationStatus::RejectedClosed) {
            return SerialDeadlineStatus::Pending;
        }
        return status == SerialOperationStatus::Timeout
            ? SerialDeadlineStatus::Expired
            : SerialDeadlineStatus::Met;
    }

    SerialOperationResult assigned(
        SerialOperationKind kind,
        SerialOperationStatus status,
        SerialDeadline deadline = {},
        std::size_t byteCount = 0,
        SerialErrorCategory category = SerialErrorCategory::None,
        std::uint32_t nativeCode = 0,
        std::optional<SerialSessionGeneration> generation = std::nullopt,
        std::optional<std::string> endpoint = std::nullopt) {
        return {
            .operation = {
                .requestId = nextDirectOperationId_++,
                .generation = generation.value_or(generation_),
                .kind = kind,
                .deadline = deadline,
            },
            .status = status,
            .deadlineStatus = deadlineStatus(status, deadline),
            .byteCount = byteCount,
            .endpoint = endpoint.value_or(options_.portName),
            .error = {
                .category = category,
                .nativeCode = nativeCode,
                .byteCount = byteCount,
            },
        };
    }

    SerialOperationResult rejected(
        SerialOperationKind kind,
        SerialOperationStatus status,
        SerialErrorCategory category,
        SerialDeadline deadline = {}) const {
        return {
            .operation = {
                .generation = generation_,
                .kind = kind,
                .deadline = deadline,
            },
            .status = status,
            .deadlineStatus = deadlineStatus(status, deadline),
            .endpoint = options_.portName,
            .error = {.category = category},
        };
    }

    SerialOperationResult rejectedClosed(
        SerialOperationKind kind,
        SerialDeadline deadline = {}) const {
        return rejected(
            kind,
            SerialOperationStatus::RejectedClosed,
            SerialErrorCategory::SessionClosed,
            deadline);
    }

    SerialOperationResult rejectedCompletion(
        svm::transport::SerialOperationId requestId,
        SerialSessionGeneration generation) const {
        return {
            .operation = {
                .requestId = requestId,
                .generation = generation,
                .kind = SerialOperationKind::Write,
            },
            .status = SerialOperationStatus::RejectedInvalid,
            .error = {.category = SerialErrorCategory::InvalidInput},
        };
    }

    SerialOperationResult admission(const SerialWriteResult& result) const {
        SerialOperationStatus status = SerialOperationStatus::RejectedInvalid;
        SerialErrorCategory category = SerialErrorCategory::InvalidInput;
        if (result.status == SerialWriteResultStatus::Accepted) {
            status = SerialOperationStatus::Accepted;
            category = SerialErrorCategory::None;
        } else if (result.status == SerialWriteResultStatus::RejectedFull) {
            status = SerialOperationStatus::RejectedFull;
            category = SerialErrorCategory::QueueFull;
        }
        return {
            .operation = {
                .requestId = result.requestId,
                .generation = result.generation,
                .kind = SerialOperationKind::Write,
                .deadline = result.deadline,
            },
            .status = status,
            .deadlineStatus = deadlineStatus(status, result.deadline),
            .byteCount = result.byteCount,
            .endpoint = options_.portName,
            .error = {
                .category = category,
                .byteCount = result.byteCount,
            },
        };
    }

    static SerialErrorCategory defaultErrorCategory(SerialOperationStatus status) noexcept {
        switch (status) {
        case SerialOperationStatus::Succeeded:
        case SerialOperationStatus::Accepted:
            return SerialErrorCategory::None;
        case SerialOperationStatus::Timeout:
            return SerialErrorCategory::Timeout;
        case SerialOperationStatus::Cancelled:
            return SerialErrorCategory::Cancelled;
        case SerialOperationStatus::Disconnected:
            return SerialErrorCategory::Disconnected;
        case SerialOperationStatus::RejectedClosed:
            return SerialErrorCategory::SessionClosed;
        case SerialOperationStatus::RejectedFull:
            return SerialErrorCategory::QueueFull;
        case SerialOperationStatus::RejectedInvalid:
            return SerialErrorCategory::InvalidInput;
        case SerialOperationStatus::Failed:
            return SerialErrorCategory::IoFailure;
        }
        return SerialErrorCategory::IoFailure;
    }

    static std::optional<SerialWriteResultStatus> queueTerminalStatus(
        SerialOperationStatus status) noexcept {
        switch (status) {
        case SerialOperationStatus::Succeeded:
            return SerialWriteResultStatus::Sent;
        case SerialOperationStatus::Failed:
            return SerialWriteResultStatus::Failed;
        case SerialOperationStatus::Timeout:
            return SerialWriteResultStatus::Timeout;
        case SerialOperationStatus::Cancelled:
            return SerialWriteResultStatus::Cancelled;
        case SerialOperationStatus::Disconnected:
            return SerialWriteResultStatus::Disconnected;
        case SerialOperationStatus::Accepted:
        case SerialOperationStatus::RejectedInvalid:
        case SerialOperationStatus::RejectedFull:
        case SerialOperationStatus::RejectedClosed:
            return std::nullopt;
        }
        return std::nullopt;
    }

    static SerialOperationStatus operationStatus(SerialWriteResultStatus status) noexcept {
        switch (status) {
        case SerialWriteResultStatus::Sent:
            return SerialOperationStatus::Succeeded;
        case SerialWriteResultStatus::Failed:
            return SerialOperationStatus::Failed;
        case SerialWriteResultStatus::Timeout:
            return SerialOperationStatus::Timeout;
        case SerialWriteResultStatus::Cancelled:
        case SerialWriteResultStatus::Closed:
            return SerialOperationStatus::Cancelled;
        case SerialWriteResultStatus::Disconnected:
            return SerialOperationStatus::Disconnected;
        case SerialWriteResultStatus::Accepted:
            return SerialOperationStatus::Accepted;
        case SerialWriteResultStatus::RejectedFull:
            return SerialOperationStatus::RejectedFull;
        case SerialWriteResultStatus::RejectedInvalid:
            return SerialOperationStatus::RejectedInvalid;
        }
        return SerialOperationStatus::Failed;
    }

    SerialTerminalResult queueTerminal(
        const SerialWriteResult& result,
        std::string endpoint,
        SerialOperationStatus status,
        SerialErrorCategory category,
        std::uint32_t nativeCode = 0) const {
        return {
            .operation = {
                .requestId = result.requestId,
                .generation = result.generation,
                .kind = SerialOperationKind::Write,
                .deadline = result.deadline,
            },
            .status = status,
            .deadlineStatus = deadlineStatus(status, result.deadline),
            .byteCount = result.byteCount,
            .endpoint = std::move(endpoint),
            .error = {
                .category = category,
                .nativeCode = nativeCode,
                .byteCount = result.byteCount,
            },
        };
    }

    void settleForClose(const std::string& endpoint) {
        if (active_.has_value()) {
            const SerialWriteResult closed = queue_.completeActive(
                active_->id,
                active_->generation,
                SerialWriteResultStatus::Closed);
            completed_.push_back(queueTerminal(
                closed,
                endpoint,
                SerialOperationStatus::Cancelled,
                SerialErrorCategory::SessionClosed));
            evidence_.push_back(completed_.back());
            active_.reset();
        }
        for (const SerialWriteResult& cancelled : queue_.cancelAllPending()) {
            completed_.push_back(queueTerminal(
                cancelled,
                endpoint,
                SerialOperationStatus::Cancelled,
                SerialErrorCategory::SessionClosed));
            evidence_.push_back(completed_.back());
        }
    }

    void recordState() {
        stateHistory_.push_back(snapshot());
    }

    svm::transport::SerialOpenOptions options_;
    SerialWriteQueue queue_;
    std::vector<std::uint8_t> received_;
    std::vector<SerialTerminalResult> completed_;
    std::vector<SerialTerminalResult> evidence_;
    std::vector<SerialSessionSnapshot> stateHistory_;
    std::optional<SerialWriteRequest> active_;
    std::optional<ScriptedOutcome> nextWrite_;
    std::optional<ScriptedOutcome> nextRead_;
    std::chrono::steady_clock::time_point now_;
    SerialSessionState state_ = SerialSessionState::Closed;
    SerialSessionGeneration generation_ = svm::transport::kUnassignedSerialSessionGeneration;
    SerialSessionGeneration generationCounter_ = svm::transport::kUnassignedSerialSessionGeneration;
    svm::transport::SerialOperationId nextDirectOperationId_ = 1'000'000;
};

static_assert(std::derived_from<FakeSerialSession, svm::transport::SerialSession>);
static_assert(std::derived_from<FakeSerialSession, svm::transport::SerialByteStream>);
static_assert(std::derived_from<FakeSerialSession, svm::transport::SerialWriteScheduler>);
static_assert(!std::derived_from<SerialWriteQueue, svm::transport::SerialWriteScheduler>);

void lifecycleTransitionsInvalidateBeforePublishingTheNextGeneration() {
    FakeSerialSession session;
    assert(session.snapshot().state == SerialSessionState::Closed);
    assert(session.snapshot().generation == 0);
    assert(session.snapshot().endpoint.empty());

    svm::transport::SerialOpenOptions firstOptions;
    firstOptions.portName = "COM5";
    firstOptions.flowControl = svm::transport::SerialFlowControl::HardwareRtsCts;
    const SerialOperationResult firstOpen = session.open(firstOptions);
    assert(firstOpen.succeeded());
    assert(firstOpen.operation.assigned());
    assert(firstOpen.operation.generation == 1);
    assert(firstOpen.endpoint == "COM5");
    assert(session.snapshot().open());
    assert(session.snapshot().usesHardwareRtsCts());

    const auto& openHistory = session.stateHistory();
    assert(openHistory.size() == 3);
    assert(openHistory[0].state == SerialSessionState::Closed);
    assert(openHistory[1].state == SerialSessionState::Opening);
    assert(openHistory[1].generation == 0);
    assert(openHistory[1].endpoint.empty());
    assert(openHistory[2].state == SerialSessionState::Open);
    assert(openHistory[2].generation == 1);

    const SerialOperationResult duplicateOpen = session.open(firstOptions);
    assert(duplicateOpen.status == SerialOperationStatus::RejectedInvalid);
    assert(session.snapshot().generation == 1);

    const SerialOperationResult closed = session.close();
    assert(closed.succeeded());
    assert(closed.operation.generation == 1);
    assert(closed.endpoint == "COM5");
    assert(session.snapshot().state == SerialSessionState::Closed);
    assert(session.snapshot().generation == 0);
    assert(session.snapshot().endpoint.empty());

    const auto& closeHistory = session.stateHistory();
    assert(closeHistory[3].state == SerialSessionState::Closing);
    assert(closeHistory[3].generation == 1);
    assert(closeHistory[4].state == SerialSessionState::Closed);
    assert(closeHistory[4].generation == 0);

    svm::transport::SerialOpenOptions secondOptions;
    secondOptions.portName = "COM6";
    const SerialOperationResult secondOpen = session.open(secondOptions);
    assert(secondOpen.succeeded());
    assert(secondOpen.operation.generation == 2);
    assert(session.snapshot().generation == 2);
    assert(session.snapshot().endpoint == "COM6");
}

void nonOpenStatesRejectEveryCapabilityWithoutMutation() {
    constexpr SerialSessionState states[]{
        SerialSessionState::Closed,
        SerialSessionState::Opening,
        SerialSessionState::Closing,
        SerialSessionState::Faulted,
    };
    for (const SerialSessionState state : states) {
        FakeSerialSession session;
        session.setNonOpenStateForTest(state);
        const SerialDeadline deadline = deadlineAt(2000);
        const SerialOperationResult write = session.byteStream().writeBytes({0x01}, deadline);
        const SerialReadResult read = session.byteStream().readAvailable(1, deadline);
        const SerialOperationResult queued = session.writeScheduler().enqueueWrite({0x02}, deadline);
        const SerialOperationResult dtr = session.setDataTerminalReady(true);
        const SerialOperationResult rts = session.setRequestToSend(true);

        for (const SerialOperationResult* result : {
                 &write,
                 &read.operation,
                 &queued,
                 &dtr,
                 &rts,
             }) {
            assert(result->status == SerialOperationStatus::RejectedClosed);
            assert(result->error.category == SerialErrorCategory::SessionClosed);
            assert(!result->operation.assigned());
        }
        assert(session.writeQueueSnapshot().empty());
        assert(session.evidence().empty());
    }
}

void typedByteResultsPreserveDeadlinesAndNativeEvidence() {
    FakeSerialSession session;
    svm::transport::SerialOpenOptions options;
    options.portName = "COM7";
    assert(session.open(options).succeeded());
    const SerialDeadline future = deadlineAt(2000);

    const std::vector<std::uint8_t> payload{1, 2, 3};
    const SerialOperationResult write = session.byteStream().writeBytes(payload, future);
    assert(write.succeeded());
    assert(write.operation.kind == SerialOperationKind::Write);
    assert(write.operation.generation == 1);
    assert(write.operation.deadline.expiresAt == future.expiresAt);
    assert(write.deadlineStatus == SerialDeadlineStatus::Met);
    assert(write.byteCount == payload.size());
    assert(write.error.ok());
    assert(write.error.byteCount == write.byteCount);
    assert(write.endpoint == "COM7");

    const SerialReadResult read = session.byteStream().readAvailable(2, future);
    assert(read.operation.succeeded());
    assert(read.operation.operation.kind == SerialOperationKind::Read);
    assert(read.bytes == std::vector<std::uint8_t>({1, 2}));
    assert(read.operation.byteCount == read.bytes.size());

    session.scriptNextWrite(
        SerialOperationStatus::Failed,
        SerialErrorCategory::IoFailure,
        2);
    const SerialOperationResult shortWrite = session.byteStream().writeBytes(payload, future);
    assert(shortWrite.status == SerialOperationStatus::Failed);
    assert(shortWrite.error.category == SerialErrorCategory::IoFailure);
    assert(shortWrite.byteCount == 2);
    assert(shortWrite.error.byteCount == 2);
    assert(shortWrite.error.nativeCode == 0);
    assert(shortWrite.operation.generation == 1);
    assert(shortWrite.operation.deadline.expiresAt == future.expiresAt);
    assert(shortWrite.endpoint == "COM7");

    session.scriptNextWrite(
        SerialOperationStatus::Failed,
        SerialErrorCategory::NativeFailure,
        1,
        1234);
    const SerialOperationResult nativeFailure = session.byteStream().writeBytes(payload, future);
    assert(nativeFailure.status == SerialOperationStatus::Failed);
    assert(nativeFailure.error.category == SerialErrorCategory::NativeFailure);
    assert(nativeFailure.error.nativeCode == 1234);
    assert(nativeFailure.byteCount == 1);
    assert(nativeFailure.error.byteCount == 1);
    assert(nativeFailure.operation.generation == 1);
    assert(nativeFailure.operation.deadline.expiresAt == future.expiresAt);
    assert(nativeFailure.endpoint == "COM7");

    session.scriptNextRead(
        SerialOperationStatus::Failed,
        SerialErrorCategory::IoFailure);
    const SerialReadResult readFailure = session.byteStream().readAvailable(8, future);
    assert(readFailure.operation.status == SerialOperationStatus::Failed);
    assert(readFailure.operation.error.category == SerialErrorCategory::IoFailure);
    assert(readFailure.operation.error.nativeCode == 0);
    assert(readFailure.operation.operation.generation == 1);
    assert(readFailure.operation.operation.deadline.expiresAt == future.expiresAt);
    assert(readFailure.operation.endpoint == "COM7");
    assert(readFailure.bytes.empty());

    session.setNowForTest(std::chrono::steady_clock::time_point(std::chrono::milliseconds(3000)));
    const SerialOperationResult timedOut = session.byteStream().writeBytes(payload, future);
    assert(timedOut.status == SerialOperationStatus::Timeout);
    assert(timedOut.deadlineStatus == SerialDeadlineStatus::Expired);
    assert(timedOut.error.category == SerialErrorCategory::Timeout);
}

void operationEvidenceUsesNeutralTypedMetadata() {
    SerialOperationResult result{
        .operation = {
            .requestId = 91,
            .generation = 12,
            .kind = SerialOperationKind::Read,
        },
        .status = SerialOperationStatus::Failed,
        .deadlineStatus = SerialDeadlineStatus::Expired,
        .byteCount = 4,
        .endpoint = "COM91",
        .error = {
            .category = SerialErrorCategory::IoFailure,
            .nativeCode = 0,
            .byteCount = 4,
            .commErrorMask = 0x0C,
            .inputQueueBytes = 32,
            .outputQueueBytes = 2,
        },
    };

    assert(svm::transport::serialOperationDirection(result.operation.kind)
        == SerialDataDirection::Receive);
    assert(std::string(svm::transport::serialDataDirectionName(SerialDataDirection::Receive)) == "rx");
    assert(std::string(svm::transport::serialOperationKindName(result.operation.kind)) == "read");
    assert(std::string(svm::transport::serialOperationStatusName(result.status)) == "failed");
    assert(std::string(svm::transport::serialDeadlineStatusName(result.deadlineStatus)) == "expired");
    assert(std::string(svm::transport::serialErrorCategoryName(result.error.category)) == "io_failure");
    assert(result.error.nativeCode == 0);
    assert(result.error.commErrorMask == 0x0C);
    assert(result.error.inputQueueBytes == 32);
    assert(result.error.outputQueueBytes == 2);
}

void schedulerUsesDualBudgetsAndKeepsActiveWorkCounted() {
    FakeSerialSession session(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 100,
    });
    svm::transport::SerialOpenOptions options;
    options.portName = "COM8";
    assert(session.open(options).succeeded());

    const SerialDeadline firstDeadline = deadlineAt(2000);
    const SerialDeadline secondDeadline = deadlineAt(2100);
    const SerialOperationResult first = session.enqueueWrite({0x01, 0x02}, firstDeadline);
    const SerialOperationResult second = session.enqueueWrite({0x03, 0x04}, secondDeadline);
    const SerialOperationResult rejected = session.enqueueWrite({0x05}, deadlineAt(2200));
    assert(first.accepted());
    assert(second.accepted());
    assert(first.operation.requestId == 1);
    assert(second.operation.requestId == 2);
    assert(first.operation.generation == 1);
    assert(first.deadlineStatus == SerialDeadlineStatus::Pending);
    assert(rejected.status == SerialOperationStatus::RejectedFull);
    assert(rejected.error.category == SerialErrorCategory::QueueFull);
    assert(!rejected.operation.assigned());

    const auto before = session.writeQueueSnapshot();
    const auto active = session.activateNextWriteForTest();
    const auto after = session.writeQueueSnapshot();
    assert(active.has_value());
    assert(active->id == first.operation.requestId);
    assert(before.countedCount() == after.countedCount());
    assert(before.countedBytes() == after.countedBytes());
    assert(after.pendingCount == 1);
    assert(after.activeCount == 1);
    assert(after.pendingBytes == 2);
    assert(after.activeBytes == 2);
    assert(session.enqueueWrite({0x06}, deadlineAt(2300)).status == SerialOperationStatus::RejectedFull);

    const SerialOperationResult completed = session.deliverCompletionForTest(
        active->id,
        active->generation,
        SerialOperationStatus::Succeeded,
        active->payloadBytes);
    assert(completed.succeeded());
    const SerialOperationResult third = session.enqueueWrite({0x06, 0x07}, deadlineAt(2400));
    assert(third.accepted());
    assert(third.operation.requestId == 3);
    assert(session.writeQueueSnapshot().countedBytes() == 4);

    FakeSerialSession byteLimited(SerialWriteQueueLimits{
        .requestCapacity = 10,
        .byteCapacity = 4,
    });
    assert(byteLimited.open(options).succeeded());
    assert(byteLimited.enqueueWrite({0x10, 0x11, 0x12}, firstDeadline).accepted());
    const SerialOperationResult byteRejected = byteLimited.enqueueWrite({0x13, 0x14}, secondDeadline);
    assert(byteRejected.status == SerialOperationStatus::RejectedFull);
    assert(byteRejected.error.category == SerialErrorCategory::QueueFull);
    assert(byteLimited.writeQueueSnapshot().countedCount() == 1);
    assert(byteLimited.writeQueueSnapshot().countedBytes() == 3);
}

void queuedDeadlineExpiryProducesOneTimeoutCompletion() {
    FakeSerialSession session(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 4,
    });
    svm::transport::SerialOpenOptions options;
    options.portName = "COM8-DEADLINE";
    assert(session.open(options).succeeded());
    const SerialDeadline deadline = deadlineAt(1500);
    const SerialOperationResult admitted = session.enqueueWrite({0x01, 0x02}, deadline);
    const auto active = session.activateNextWriteForTest();
    assert(admitted.accepted());
    assert(active.has_value());

    session.setNowForTest(std::chrono::steady_clock::time_point(std::chrono::milliseconds(1600)));
    const SerialOperationResult timedOut = session.deliverCompletionForTest(
        active->id,
        active->generation,
        SerialOperationStatus::Succeeded,
        active->payloadBytes);
    assert(timedOut.status == SerialOperationStatus::Timeout);
    assert(timedOut.deadlineStatus == SerialDeadlineStatus::Expired);
    assert(timedOut.error.category == SerialErrorCategory::Timeout);
    assert(timedOut.operation.deadline.expiresAt == deadline.expiresAt);
    assert(session.completionCount() == 1);
    assert(session.writeQueueSnapshot().empty());

    const SerialOperationResult duplicate = session.deliverCompletionForTest(
        active->id,
        active->generation,
        SerialOperationStatus::Succeeded,
        active->payloadBytes);
    assert(duplicate.status == SerialOperationStatus::RejectedInvalid);
    assert(session.completionCount() == 1);
    assert(session.takeCompletedWrites().size() == 1);
    assert(session.takeCompletedWrites().empty());
}

void cancellationAndCloseSettleEachAcceptedWriteExactlyOnce() {
    FakeSerialSession cancellationSession(SerialWriteQueueLimits{
        .requestCapacity = 3,
        .byteCapacity = 6,
    });
    svm::transport::SerialOpenOptions options;
    options.portName = "COM9";
    assert(cancellationSession.open(options).succeeded());
    const SerialOperationResult activeAdmission = cancellationSession.enqueueWrite({0x01, 0x02}, deadlineAt(2000));
    const SerialOperationResult pendingAdmission = cancellationSession.enqueueWrite({0x03, 0x04}, deadlineAt(2100));
    assert(cancellationSession.activateNextWriteForTest().has_value());

    const SerialOperationResult activeCancelled = cancellationSession.cancelActiveWriteForTest();
    assert(activeCancelled.status == SerialOperationStatus::Cancelled);
    assert(activeCancelled.operation.requestId == activeAdmission.operation.requestId);
    assert(cancellationSession.cancelActiveWriteForTest().status == SerialOperationStatus::RejectedInvalid);
    const std::vector<SerialTerminalResult> pendingCancelled = cancellationSession.cancelPendingWrites();
    assert(pendingCancelled.size() == 1);
    assert(pendingCancelled.front().status == SerialOperationStatus::Cancelled);
    assert(pendingCancelled.front().operation.requestId == pendingAdmission.operation.requestId);
    assert(cancellationSession.cancelPendingWrites().empty());

    const std::vector<SerialTerminalResult> activeResults = cancellationSession.takeCompletedWrites();
    assert(activeResults.size() == 1);
    assert(activeResults.front().operation.requestId == activeAdmission.operation.requestId);
    assert(cancellationSession.takeCompletedWrites().empty());
    assert(cancellationSession.evidence().size() == 2);
    assert(cancellationSession.writeQueueSnapshot().empty());

    FakeSerialSession closeSession(SerialWriteQueueLimits{
        .requestCapacity = 3,
        .byteCapacity = 6,
    });
    assert(closeSession.open(options).succeeded());
    const SerialOperationResult closeActive = closeSession.enqueueWrite({0x10, 0x11}, deadlineAt(2200));
    const SerialOperationResult closePending = closeSession.enqueueWrite({0x12}, deadlineAt(2300));
    assert(closeSession.activateNextWriteForTest().has_value());
    assert(!closeSession.activateNextWriteForTest().has_value());
    assert(closeSession.close().succeeded());
    assert(closeSession.snapshot().generation == 0);
    assert(closeSession.writeQueueSnapshot().empty());

    const std::vector<SerialTerminalResult> closedResults = closeSession.takeCompletedWrites();
    assert(closedResults.size() == 2);
    assert(closedResults[0].operation.requestId == closeActive.operation.requestId);
    assert(closedResults[1].operation.requestId == closePending.operation.requestId);
    assert(closedResults[0].operation.deadline.expiresAt == deadlineAt(2200).expiresAt);
    assert(closedResults[1].operation.deadline.expiresAt == deadlineAt(2300).expiresAt);
    for (const SerialTerminalResult& result : closedResults) {
        assert(result.status == SerialOperationStatus::Cancelled);
        assert(result.deadlineStatus == SerialDeadlineStatus::Met);
        assert(result.error.category == SerialErrorCategory::SessionClosed);
        assert(result.operation.generation == 1);
        assert(result.endpoint == "COM9");
        assert(result.byteCount == 0);
        assert(result.error.byteCount == 0);
    }
    assert(closeSession.takeCompletedWrites().empty());
    assert(closeSession.open(options).operation.generation == 2);
    assert(closeSession.takeCompletedWrites().empty());
}

void staleAndDuplicateCompletionsCannotMutateTheReplacementSession() {
    FakeSerialSession session(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 4,
    });
    svm::transport::SerialOpenOptions firstOptions;
    firstOptions.portName = "COM10";
    assert(session.open(firstOptions).succeeded());
    const SerialOperationResult oldAdmission = session.enqueueWrite({0x01, 0x02}, deadlineAt(2000));
    assert(session.activateNextWriteForTest().has_value());
    assert(session.close().succeeded());
    assert(session.takeCompletedWrites().size() == 1);

    svm::transport::SerialOpenOptions replacementOptions;
    replacementOptions.portName = "COM11";
    assert(session.open(replacementOptions).operation.generation == 2);
    const SerialOperationResult newAdmission = session.enqueueWrite({0x03, 0x04}, deadlineAt(3000));
    const auto active = session.activateNextWriteForTest();
    assert(active.has_value());
    const auto before = session.writeQueueSnapshot();
    const std::size_t evidenceBefore = session.evidence().size();

    const SerialOperationResult staleGeneration = session.deliverCompletionForTest(
        newAdmission.operation.requestId,
        oldAdmission.operation.generation,
        SerialOperationStatus::Succeeded,
        newAdmission.byteCount);
    assert(staleGeneration.status == SerialOperationStatus::RejectedInvalid);
    assert(staleGeneration.error.category == SerialErrorCategory::InvalidInput);
    const SerialOperationResult staleRequest = session.deliverCompletionForTest(
        oldAdmission.operation.requestId,
        newAdmission.operation.generation,
        SerialOperationStatus::Succeeded,
        oldAdmission.byteCount);
    assert(staleRequest.status == SerialOperationStatus::RejectedInvalid);
    assert(staleRequest.error.category == SerialErrorCategory::InvalidInput);
    assert(session.snapshot().state == SerialSessionState::Open);
    assert(session.snapshot().generation == 2);
    assert(session.snapshot().endpoint == "COM11");
    const auto afterStale = session.writeQueueSnapshot();
    assert(afterStale.pendingCount == before.pendingCount);
    assert(afterStale.activeCount == before.activeCount);
    assert(afterStale.pendingBytes == before.pendingBytes);
    assert(afterStale.activeBytes == before.activeBytes);
    assert(session.completionCount() == 0);
    assert(session.evidence().size() == evidenceBefore);

    const SerialOperationResult completed = session.deliverCompletionForTest(
        newAdmission.operation.requestId,
        newAdmission.operation.generation,
        SerialOperationStatus::Succeeded,
        newAdmission.byteCount);
    assert(completed.succeeded());
    assert(session.completionCount() == 1);
    const SerialOperationResult duplicate = session.deliverCompletionForTest(
        newAdmission.operation.requestId,
        newAdmission.operation.generation,
        SerialOperationStatus::Succeeded,
        newAdmission.byteCount);
    assert(duplicate.status == SerialOperationStatus::RejectedInvalid);
    assert(session.completionCount() == 1);
    assert(session.evidence().size() == evidenceBefore + 1);
    assert(session.writeQueueSnapshot().empty());
}

} // namespace

int main() {
    lifecycleTransitionsInvalidateBeforePublishingTheNextGeneration();
    nonOpenStatesRejectEveryCapabilityWithoutMutation();
    typedByteResultsPreserveDeadlinesAndNativeEvidence();
    operationEvidenceUsesNeutralTypedMetadata();
    schedulerUsesDualBudgetsAndKeepsActiveWorkCounted();
    queuedDeadlineExpiryProducesOneTimeoutCompletion();
    cancellationAndCloseSettleEachAcceptedWriteExactlyOnce();
    staleAndDuplicateCompletionsCannotMutateTheReplacementSession();
    return 0;
}
