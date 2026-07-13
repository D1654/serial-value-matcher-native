#include "transport/serial_write_queue.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using svm::transport::SerialWriteQueue;
using svm::transport::SerialWriteQueueLimits;
using svm::transport::SerialWriteResultStatus;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

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
    assert(queue.snapshot().activeCount == 1);
}

void defaultCountBudgetRejectsTheSixtyFifthRequest() {
    SerialWriteQueue queue;
    for (std::size_t index = 0; index < svm::transport::kDefaultSerialWriteQueueCapacity; ++index) {
        assert(queue.enqueue({static_cast<std::uint8_t>(index)}).accepted());
    }

    const auto rejected = queue.enqueue({0xFF});
    assert(rejected.status == SerialWriteResultStatus::RejectedFull);
    assert(rejected.rejected());
    assert(contains(rejected.message, "队列已满"));
    assert(queue.snapshot().countedCount() == svm::transport::kDefaultSerialWriteQueueCapacity);
    assert(queue.peek()->id == 1);
}

void byteBudgetRejectsOverflowWithoutDroppingExistingWork() {
    SerialWriteQueue queue;
    std::vector<std::uint8_t> fullPayload(
        svm::transport::kDefaultSerialWriteQueueByteCapacity,
        0x5A);
    const auto accepted = queue.enqueue(std::move(fullPayload), 1000, 3);
    const auto rejected = queue.enqueue({0x01});

    assert(accepted.accepted());
    assert(rejected.status == SerialWriteResultStatus::RejectedFull);
    assert(queue.snapshot().pendingBytes == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(queue.peek()->id == accepted.requestId);

    const auto active = queue.activateNext();
    assert(active.has_value());
    assert(queue.snapshot().activeBytes == svm::transport::kDefaultSerialWriteQueueByteCapacity);
    assert(queue.enqueue({0x02}).status == SerialWriteResultStatus::RejectedFull);

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
    assert(after.full());
    assert(queue.enqueue({0x05}).status == SerialWriteResultStatus::RejectedFull);

    const auto failed = queue.completeActive(
        active->id,
        active->generation,
        SerialWriteResultStatus::Failed,
        1,
        "write failed");
    assert(failed.status == SerialWriteResultStatus::Failed);
    assert(failed.byteCount == 1);
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
            transferred,
            status == SerialWriteResultStatus::Failed ? "write failed" : "");
        assert(result.status == status);
        assert(result.requestId == accepted.requestId);
        assert(result.generation == 12);
        assert(result.byteCount == transferred);
        assert(result.terminal());
        assert(queue.empty());
        assert(queue.snapshot().countedBytes() == 0);

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
    assert(contains(result.message, "字节数不完整"));
    assert(queue.empty());
}

void clearReleasesPendingAndActiveBudgets() {
    SerialWriteQueue queue(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 4,
    });
    queue.enqueue({0x01, 0x02});
    queue.enqueue({0x03, 0x04});
    assert(queue.activateNext().has_value());
    assert(queue.snapshot().countedCount() == 2);
    assert(queue.snapshot().countedBytes() == 4);

    queue.clear();
    assert(queue.empty());
    assert(queue.snapshot().countedCount() == 0);
    assert(queue.snapshot().countedBytes() == 0);
}

void snapshotExposesBothConfiguredBudgets() {
    SerialWriteQueue queue(SerialWriteQueueLimits{
        .requestCapacity = 2,
        .byteCapacity = 6,
    });
    auto snapshot = queue.snapshot();
    assert(snapshot.capacity == 2);
    assert(snapshot.byteCapacity == 6);
    assert(snapshot.pendingCount == 0);
    assert(snapshot.activeCount == 0);
    assert(snapshot.empty());
    assert(!snapshot.full());

    queue.enqueue({0x01, 0x02});
    queue.enqueue({0x03});
    assert(queue.activateNext().has_value());
    snapshot = queue.snapshot();
    assert(snapshot.pendingCount == 1);
    assert(snapshot.activeCount == 1);
    assert(snapshot.pendingBytes == 1);
    assert(snapshot.activeBytes == 2);
    assert(snapshot.countedCount() == 2);
    assert(snapshot.countedBytes() == 3);
    assert(snapshot.full());
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
    clearReleasesPendingAndActiveBudgets();
    snapshotExposesBothConfiguredBudgets();
    resultStatusNamesStayStableForLogsAndTests();

    std::cout << "serial_write_queue_tests passed\n";
    return 0;
}
