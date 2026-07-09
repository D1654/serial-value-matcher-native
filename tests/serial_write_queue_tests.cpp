#include "transport/serial_write_queue.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using svm::transport::SerialWriteQueue;
using svm::transport::SerialWriteResultStatus;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void enqueueAssignsIdsAndPreservesFifo() {
    SerialWriteQueue queue(4);
    const auto first = queue.enqueue({0x01, 0x02}, 250);
    const auto second = queue.enqueue({0x03}, 500);

    assert(first.status == SerialWriteResultStatus::Accepted);
    assert(first.accepted());
    assert(first.requestId == 1);
    assert(first.byteCount == 2);
    assert(second.requestId == 2);
    assert(queue.pendingCount() == 2);

    const auto front = queue.peek();
    assert(front.has_value());
    assert(front->id == first.requestId);
    assert(front->cancellationToken.valid());
    assert(front->timeoutMs == 250);
    assert(front->payload == std::vector<std::uint8_t>({0x01, 0x02}));

    const auto taken = queue.takeNext();
    assert(taken.has_value());
    assert(taken->id == first.requestId);
    assert(queue.peek()->id == second.requestId);
}

void fullQueueReportsBackpressureWithoutDroppingExistingRequests() {
    SerialWriteQueue queue(1);
    const auto accepted = queue.enqueue({0x10});
    const auto rejected = queue.enqueue({0x11});

    assert(accepted.status == SerialWriteResultStatus::Accepted);
    assert(rejected.status == SerialWriteResultStatus::RejectedFull);
    assert(rejected.rejected());
    assert(contains(rejected.message, "队列已满"));
    assert(queue.pendingCount() == 1);
    assert(queue.peek()->id == accepted.requestId);
}

void invalidRequestsAreRejected() {
    SerialWriteQueue queue;
    const auto empty = queue.enqueue({});
    const auto badTimeout = queue.enqueue({0x01}, 0);

    assert(empty.status == SerialWriteResultStatus::RejectedInvalid);
    assert(badTimeout.status == SerialWriteResultStatus::RejectedInvalid);
    assert(queue.empty());
}

void cancelBeforeSendRemovesOnlyTargetRequest() {
    SerialWriteQueue queue(3);
    const auto first = queue.enqueue({0x01});
    const auto second = queue.enqueue({0x02});

    const auto cancelled = queue.cancelPending(first.requestId);
    assert(cancelled.status == SerialWriteResultStatus::Cancelled);
    assert(cancelled.terminal());
    assert(cancelled.requestId == first.requestId);
    assert(cancelled.byteCount == 1);
    assert(queue.pendingCount() == 1);
    assert(queue.peek()->id == second.requestId);

    const auto missing = queue.cancelPending(first.requestId);
    assert(missing.status == SerialWriteResultStatus::RejectedInvalid);
}

void cancelAllPendingReturnsTerminalResultsInFifoOrder() {
    SerialWriteQueue queue(4);
    const auto first = queue.enqueue({0x01, 0x02});
    const auto second = queue.enqueue({0x03});

    const auto cancelled = queue.cancelAllPending();
    assert(cancelled.size() == 2);
    assert(cancelled[0].status == SerialWriteResultStatus::Cancelled);
    assert(cancelled[0].terminal());
    assert(cancelled[0].requestId == first.requestId);
    assert(cancelled[0].byteCount == 2);
    assert(cancelled[1].requestId == second.requestId);
    assert(cancelled[1].byteCount == 1);
    assert(queue.empty());
}

void completionResultsPopTheFrontRequest() {
    SerialWriteQueue queue(3);
    const auto sentRequest = queue.enqueue({0x01, 0x02});
    const auto failedRequest = queue.enqueue({0x03});
    const auto timeoutRequest = queue.enqueue({0x04});

    const auto sent = queue.completeNextSent(2);
    assert(sent.status == SerialWriteResultStatus::Sent);
    assert(sent.terminal());
    assert(sent.requestId == sentRequest.requestId);
    assert(sent.byteCount == 2);

    const auto failed = queue.completeNextFailed("write failed");
    assert(failed.status == SerialWriteResultStatus::Failed);
    assert(failed.requestId == failedRequest.requestId);
    assert(failed.message == "write failed");

    const auto timeout = queue.completeNextTimeout("timeout");
    assert(timeout.status == SerialWriteResultStatus::Timeout);
    assert(timeout.requestId == timeoutRequest.requestId);
    assert(timeout.message == "timeout");
    assert(queue.empty());
}

void partialCompletionIsAFailedTerminalResult() {
    SerialWriteQueue queue;
    const auto request = queue.enqueue({0x01, 0x02, 0x03});
    const auto result = queue.completeNextSent(2);

    assert(result.status == SerialWriteResultStatus::Failed);
    assert(result.terminal());
    assert(result.requestId == request.requestId);
    assert(result.byteCount == 2);
    assert(contains(result.message, "字节数不完整"));
    assert(queue.empty());
}

void snapshotExposesBoundedQueueState() {
    SerialWriteQueue queue(2);
    auto snapshot = queue.snapshot();
    assert(snapshot.capacity == 2);
    assert(snapshot.pendingCount == 0);
    assert(snapshot.empty());
    assert(!snapshot.full());

    queue.enqueue({0x01});
    queue.enqueue({0x02});
    snapshot = queue.snapshot();
    assert(snapshot.pendingCount == 2);
    assert(snapshot.full());
    assert(snapshot.nextRequestId == 3);
}

void resultStatusNamesStayStableForLogsAndTests() {
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Accepted)) == "accepted");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::RejectedFull)) == "rejected-full");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Sent)) == "sent");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Timeout)) == "timeout");
    assert(std::string(svm::transport::serialWriteResultStatusName(SerialWriteResultStatus::Cancelled)) == "cancelled");
    assert(svm::transport::isSerialWriteResultRejected(SerialWriteResultStatus::RejectedInvalid));
    assert(svm::transport::isSerialWriteResultRejected(SerialWriteResultStatus::RejectedFull));
    assert(!svm::transport::isSerialWriteResultRejected(SerialWriteResultStatus::Accepted));
    assert(svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Sent));
    assert(svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Timeout));
    assert(!svm::transport::isSerialWriteResultTerminal(SerialWriteResultStatus::Accepted));
}

} // namespace

int main() {
    enqueueAssignsIdsAndPreservesFifo();
    fullQueueReportsBackpressureWithoutDroppingExistingRequests();
    invalidRequestsAreRejected();
    cancelBeforeSendRemovesOnlyTargetRequest();
    cancelAllPendingReturnsTerminalResultsInFifoOrder();
    completionResultsPopTheFrontRequest();
    partialCompletionIsAFailedTerminalResult();
    snapshotExposesBoundedQueueState();
    resultStatusNamesStayStableForLogsAndTests();

    std::cout << "serial_write_queue_tests passed\n";
    return 0;
}
