#include "transport/serial_write_queue.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using svm::transport::SerialWriteQueue;
using svm::transport::SerialWriteQueueLimits;
using svm::transport::SerialWriteResultStatus;

void enqueueAssignsIdsAndPreservesFifoMetadata() {
    SerialWriteQueue queue(4);
    const auto expiresAt = std::chrono::steady_clock::time_point(std::chrono::milliseconds(12345));
    const auto first = queue.enqueue(
        {0x01, 0x02},
        250,
        7,
        {.expiresAt = expiresAt});
    const auto second = queue.enqueue({0x03}, 500, 7);

    assert(first.status == SerialWriteResultStatus::Accepted);
    assert(first.accepted());
    assert(first.requestId == 1);
    assert(first.generation == 7);
    assert(first.byteCount == 2);
    assert(first.deadline.expiresAt == expiresAt);
    assert(second.requestId == 2);
    assert(second.deadline.set());
    assert(queue.pendingCount() == 2);

    const auto front = queue.peek();
    assert(front.has_value());
    assert(front->id == first.requestId);
    assert(front->generation == 7);
    assert(front->cancellationToken.valid());
    assert(front->cancellationToken.generation == 7);
    assert(front->payloadBytes == 2);
    assert(front->timeoutMs == 250);
    assert(front->deadline.expiresAt == expiresAt);
    assert(front->payload == std::vector<std::uint8_t>({0x01, 0x02}));

    const auto active = queue.activateNext();
    assert(active.has_value());
    assert(active->id == first.requestId);
    assert(queue.peek()->id == second.requestId);
    const auto snapshot = queue.snapshot();
    assert(snapshot.generation == 7);
    assert(snapshot.activeCount == 1);
    assert(snapshot.activeRequestId == first.requestId);
    assert(snapshot.highWaterCount == 2);
    assert(snapshot.highWaterBytes == 3);
}

void defaultCountBudgetRejectsTheSixtyFifthRequest() {
    SerialWriteQueue queue;
    for (std::size_t index = 0; index < svm::transport::kDefaultSerialWriteQueueCapacity; ++index) {
        assert(queue.enqueue({static_cast<std::uint8_t>(index)}).accepted());
    }

    const auto beforeRejection = queue.snapshot();
    const auto rejected = queue.enqueue({0xFF});
    const auto afterRejection = queue.snapshot();
    assert(rejected.status == SerialWriteResultStatus::RejectedFull);
    assert(rejected.rejected());
    assert(afterRejection.countedCount() == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(afterRejection.nextRequestId == beforeRejection.nextRequestId);
    assert(afterRejection.highWaterCount == beforeRejection.highWaterCount);
    assert(afterRejection.highWaterBytes == beforeRejection.highWaterBytes);
    assert(queue.peek()->id == 1);
}

void byteBudgetRejectsOverflowWithoutDroppingExistingWork() {
    SerialWriteQueue queue;
    std::vector<std::uint8_t> fullPayload(
        svm::transport::kDefaultSerialWriteQueueByteCapacity,
        0x5A);
    const auto accepted = queue.enqueue(std::move(fullPayload), 1000, 3);
    const auto rejected = queue.enqueue({0x01}, 1000, 3);

    assert(accepted.accepted());
    assert(rejected.status == SerialWriteResultStatus::RejectedFull);
    const auto fullSnapshot = queue.snapshot();
    assert(fullSnapshot.pendingBytes == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(fullSnapshot.highWaterCount == 1);
    assert(fullSnapshot.highWaterBytes == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(fullSnapshot.nextRequestId == 2);
    assert(queue.peek()->id == accepted.requestId);

    const auto active = queue.activateNext();
    assert(active.has_value());
    assert(queue.snapshot().activeBytes == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(queue.enqueue({0x02}, 1000, 3).status == SerialWriteResultStatus::RejectedFull);

    SerialWriteQueue oversizedQueue;
    std::vector<std::uint8_t> oversized(
        svm::transport::kDefaultSerialWriteQueueByteCapacity + 1,
        0xA5);
    assert(oversizedQueue.enqueue(std::move(oversized)).status == SerialWriteResultStatus::RejectedFull);
    assert(oversizedQueue.empty());
}

void invalidRequestsAndLimitsAreRejected() {
    SerialWriteQueue queue;
    assert(queue.enqueue({}).status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.enqueue({0x01}, 0).status == SerialWriteResultStatus::RejectedInvalid);

    SerialWriteQueue zeroCount(SerialWriteQueueLimits{
        .requestCapacity = 0,
        .byteCapacity = 4,
    });
    SerialWriteQueue zeroBytes(SerialWriteQueueLimits{
        .requestCapacity = 4,
        .byteCapacity = 0,
    });
    assert(zeroCount.enqueue({0x01}).status == SerialWriteResultStatus::RejectedInvalid);
    assert(zeroBytes.enqueue({0x01}).status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.empty());
}

void activeWorkRemainsInsideBothBudgets() {
    SerialWriteQueue queue(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 4,
    });
    const auto first = queue.enqueue({0x01, 0x02}, 1000, 11);
    const auto second = queue.enqueue({0x03, 0x04}, 1000, 11);
    assert(first.accepted());
    assert(second.accepted());

    const auto before = queue.snapshot();
    const auto active = queue.activateNext();
    const auto after = queue.snapshot();
    assert(active.has_value());
    assert(before.countedCount() == after.countedCount());
    assert(before.countedBytes() == after.countedBytes());
    assert(after.pendingCount == 1);
    assert(after.activeCount == 1);
    assert(after.pendingBytes == 2);
    assert(after.activeBytes == 2);
    assert(after.activeRequestId == first.requestId);
    assert(after.highWaterCount == 2);
    assert(after.highWaterBytes == 4);
    assert(after.full());
    assert(queue.enqueue({0x05}, 1000, 11).status == SerialWriteResultStatus::RejectedFull);

    const auto failed = queue.completeActive(
        active->id,
        active->generation,
        SerialWriteResultStatus::Failed,
        1);
    assert(failed.status == SerialWriteResultStatus::Failed);
    assert(failed.byteCount == 1);
    const auto afterTerminal = queue.snapshot();
    assert(afterTerminal.activeRequestId == svm::transport::kUnassignedSerialOperationId);
    assert(afterTerminal.highWaterCount == 2);
    assert(afterTerminal.highWaterBytes == 4);
    assert(queue.enqueue({0x05, 0x06}, 1000, 11).accepted());
    assert(queue.snapshot().countedBytes() == 4);
}

void cancelBeforeSendReleasesOnlyTheTargetReservation() {
    SerialWriteQueue queue(SerialWriteQueueLimits{
        .requestCapacity = 3,
        .byteCapacity = 8,
    });
    const auto first = queue.enqueue({0x01, 0x02}, 1000, 4);
    const auto second = queue.enqueue({0x03}, 1000, 4);

    const auto cancelled = queue.cancelPending(first.requestId);
    assert(cancelled.status == SerialWriteResultStatus::Cancelled);
    assert(cancelled.terminal());
    assert(cancelled.requestId == first.requestId);
    assert(cancelled.generation == 4);
    assert(cancelled.byteCount == 0);
    assert(queue.pendingCount() == 1);
    assert(queue.snapshot().pendingBytes == 1);
    assert(queue.peek()->id == second.requestId);

    const auto missing = queue.cancelPending(first.requestId);
    assert(missing.status == SerialWriteResultStatus::RejectedInvalid);
    const auto wrongGeneration = queue.cancelPending({
        .requestId = second.requestId,
        .generation = 99,
    });
    assert(wrongGeneration.status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.pendingCount() == 1);
}

void cancelAllPendingReturnsTerminalResultsInFifoOrder() {
    SerialWriteQueue queue(4);
    const auto first = queue.enqueue({0x01, 0x02}, 1000, 5);
    const auto second = queue.enqueue({0x03}, 1000, 5);

    const auto cancelled = queue.cancelAllPending();
    assert(cancelled.size() == 2);
    assert(cancelled[0].status == SerialWriteResultStatus::Cancelled);
    assert(cancelled[0].terminal());
    assert(cancelled[0].requestId == first.requestId);
    assert(cancelled[0].byteCount == 0);
    assert(cancelled[1].requestId == second.requestId);
    assert(cancelled[1].byteCount == 0);
    assert(queue.empty());
    assert(queue.snapshot().countedBytes() == 0);
    assert(queue.cancelAllPending().empty());
}

void everyActiveTerminalPathReleasesExactlyOnce() {
    constexpr std::array terminalStatuses{
        SerialWriteResultStatus::Sent,
        SerialWriteResultStatus::Failed,
        SerialWriteResultStatus::Timeout,
        SerialWriteResultStatus::Cancelled,
        SerialWriteResultStatus::Disconnected,
        SerialWriteResultStatus::Closed,
    };

    for (const SerialWriteResultStatus status : terminalStatuses) {
        SerialWriteQueue queue(SerialWriteQueueLimits{
            .requestCapacity = 1,
            .byteCapacity = 3,
        });
        const auto accepted = queue.enqueue({0x01, 0x02, 0x03}, 1000, 12);
        const auto active = queue.activateNext();
        assert(accepted.accepted());
        assert(active.has_value());
        assert(queue.full());

        const std::size_t transferred = status == SerialWriteResultStatus::Sent ? 3 : 1;
        const auto result = queue.completeActive(
            active->id,
            active->generation,
            status,
            transferred);
        assert(result.status == status);
        assert(result.requestId == accepted.requestId);
        assert(result.generation == 12);
        assert(result.byteCount == transferred);
        assert(result.terminal());
        assert(queue.empty());
        const auto terminalSnapshot = queue.snapshot();
        assert(terminalSnapshot.countedBytes() == 0);
        assert(terminalSnapshot.activeRequestId == svm::transport::kUnassignedSerialOperationId);
        assert(terminalSnapshot.highWaterCount == 1);
        assert(terminalSnapshot.highWaterBytes == 3);

        const auto duplicate = queue.completeActive(
            active->id,
            active->generation,
            status,
            transferred);
        assert(duplicate.status == SerialWriteResultStatus::RejectedInvalid);
        assert(queue.empty());
    }
}

void mismatchedCompletionCannotReleaseTheActiveReservation() {
    SerialWriteQueue queue;
    const auto accepted = queue.enqueue({0x01, 0x02}, 1000, 17);
    const auto active = queue.activateNext();
    assert(active.has_value());

    assert(queue.completeActive(
        accepted.requestId + 1,
        17,
        SerialWriteResultStatus::Sent,
        2).status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.completeActive(
        accepted.requestId,
        18,
        SerialWriteResultStatus::Sent,
        2).status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.completeActive(
        accepted.requestId,
        17,
        SerialWriteResultStatus::Accepted,
        2).status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.completeActive(
        accepted.requestId,
        17,
        SerialWriteResultStatus::Failed,
        3).status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.snapshot().activeCount == 1);
    assert(queue.snapshot().activeBytes == 2);

    assert(queue.completeActive(
        accepted.requestId,
        17,
        SerialWriteResultStatus::Sent,
        2).status == SerialWriteResultStatus::Sent);
    assert(queue.empty());
}

void partialSentCompletionBecomesFailedAndReleasesReservation() {
    SerialWriteQueue queue;
    const auto request = queue.enqueue({0x01, 0x02, 0x03}, 1000, 9);
    const auto active = queue.activateNext();
    assert(active.has_value());
    const auto result = queue.completeActive(
        request.requestId,
        9,
        SerialWriteResultStatus::Sent,
        2);

    assert(result.status == SerialWriteResultStatus::Failed);
    assert(result.terminal());
    assert(result.requestId == request.requestId);
    assert(result.byteCount == 2);
    assert(queue.empty());
}

void generationTransitionsRequireAnEmptyQueueAndResetHighWater() {
    SerialWriteQueue queue(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 4,
    });
    assert(queue.beginGeneration(21));
    const auto accepted = queue.enqueue({0x01, 0x02}, 1000, 21);
    assert(accepted.accepted());
    assert(!queue.beginGeneration(22));
    const auto beforeMismatch = queue.snapshot();
    const auto mismatched = queue.enqueue({0x03}, 1000, 22);
    const auto afterMismatch = queue.snapshot();
    assert(mismatched.status == SerialWriteResultStatus::RejectedInvalid);
    assert(mismatched.generation == 22);
    assert(afterMismatch.generation == 21);
    assert(afterMismatch.nextRequestId == beforeMismatch.nextRequestId);
    assert(afterMismatch.highWaterCount == beforeMismatch.highWaterCount);
    assert(afterMismatch.highWaterBytes == beforeMismatch.highWaterBytes);
    assert(queue.snapshot().nextRequestId == 2);

    const auto active = queue.activateNext();
    assert(active.has_value());
    assert(queue.completeActive(
        active->id,
        active->generation,
        SerialWriteResultStatus::Sent,
        2).status == SerialWriteResultStatus::Sent);
    const auto completed = queue.snapshot();
    assert(completed.generation == 21);
    assert(completed.highWaterCount == 1);
    assert(completed.highWaterBytes == 2);

    assert(!queue.beginGeneration(svm::transport::kUnassignedSerialSessionGeneration));
    assert(queue.beginGeneration(22));
    const auto reset = queue.snapshot();
    assert(reset.generation == 22);
    assert(reset.highWaterCount == 0);
    assert(reset.highWaterBytes == 0);
    assert(reset.nextRequestId == 2);

    SerialWriteQueue adopted(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 4,
    });
    const auto unassigned = adopted.enqueue({0x01, 0x02, 0x03});
    const auto unassignedActive = adopted.activateNext();
    assert(unassignedActive.has_value());
    assert(adopted.completeActive(
        unassigned.requestId,
        unassigned.generation,
        SerialWriteResultStatus::Sent,
        3).status == SerialWriteResultStatus::Sent);
    assert(adopted.snapshot().highWaterBytes == 3);
    assert(adopted.enqueue({0x04}, 1000, 31).accepted());
    assert(adopted.snapshot().generation == 31);
    assert(adopted.snapshot().highWaterBytes == 1);
}

void snapshotExposesBothConfiguredBudgets() {
    SerialWriteQueue queue(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 6,
    });
    assert(queue.beginGeneration(23));
    auto snapshot = queue.snapshot();
    assert(snapshot.generation == 23);
    assert(snapshot.capacity == 2);
    assert(snapshot.byteCapacity == 6);
    assert(snapshot.pendingCount == 0);
    assert(snapshot.activeCount == 0);
    assert(snapshot.empty());
    assert(!snapshot.full());

    queue.enqueue({0x01, 0x02}, 1000, 23);
    queue.enqueue({0x03}, 1000, 23);
    assert(queue.activateNext().has_value());
    snapshot = queue.snapshot();
    assert(snapshot.pendingCount == 1);
    assert(snapshot.activeCount == 1);
    assert(snapshot.activeRequestId == 1);
    assert(snapshot.pendingBytes == 1);
    assert(snapshot.activeBytes == 2);
    assert(snapshot.countedCount() == 2);
    assert(snapshot.countedBytes() == 3);
    assert(snapshot.full());
    assert(snapshot.highWaterCount == 2);
    assert(snapshot.highWaterBytes == 3);
    assert(snapshot.nextRequestId == 3);
}

void resultStatusNamesStayStableForLogsAndTests() {
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Accepted)) == "accepted");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::RejectedFull)) == "rejected-full");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Sent)) == "sent");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Timeout)) == "timeout");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Cancelled)) == "cancelled");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Disconnected)) == "disconnected");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Closed)) == "closed");
    assert(svm::transport::isSerialWriteResultRejected(SerialWriteResultStatus::RejectedInvalid));
    assert(svm::transport::isSerialWriteResultRejected(SerialWriteResultStatus::RejectedFull));
    assert(!svm::transport::isSerialWriteResultRejected(SerialWriteResultStatus::Accepted));
    assert(svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Sent));
    assert(svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Timeout));
    assert(svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Closed));
    assert(!svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Accepted));
}

} // namespace

int main() {
    enqueueAssignsIdsAndPreservesFifoMetadata();
    defaultCountBudgetRejectsTheSixtyFifthRequest();
    byteBudgetRejectsOverflowWithoutDroppingExistingWork();
    invalidRequestsAndLimitsAreRejected();
    activeWorkRemainsInsideBothBudgets();
    cancelBeforeSendReleasesOnlyTheTargetReservation();
    cancelAllPendingReturnsTerminalResultsInFifoOrder();
    everyActiveTerminalPathReleasesExactlyOnce();
    mismatchedCompletionCannotReleaseTheActiveReservation();
    partialSentCompletionBecomesFailedAndReleasesReservation();
    generationTransitionsRequireAnEmptyQueueAndResetHighWater();
    snapshotExposesBothConfiguredBudgets();
    resultStatusNamesStayStableForLogsAndTests();

    std::cout << "serial_write_queue_tests passed\n";
    return 0;
}
