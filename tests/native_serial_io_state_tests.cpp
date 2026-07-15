#include "win32/native_serial_io_state.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using svm::win32::NativeSerialIoOwner;
using svm::win32::NativeSerialReadDecision;
using svm::win32::NativeSerialIoState;
using svm::win32::NativeSerialWriteCompletionDecision;
using svm::win32::NativeSerialWriteKey;

svm::transport::SerialWriteQueueSnapshot queueSnapshot(
    std::size_t pendingCount,
    std::size_t activeCount,
    std::size_t capacity = 4) {
    return {
        .capacity = capacity,
        .byteCapacity = 1024,
        .pendingCount = pendingCount,
        .activeCount = activeCount,
        .pendingBytes = pendingCount * 16,
        .activeBytes = activeCount * 16,
    };
}

svm::transport::SerialSessionSnapshot openSession(
    svm::transport::SerialSessionGeneration generation) {
    return {
        .state = svm::transport::SerialSessionState::Open,
        .generation = generation,
    };
}

svm::transport::SerialTerminalResult terminalWrite(
    svm::transport::SerialSessionGeneration generation,
    svm::transport::SerialOperationId requestId,
    svm::transport::SerialOperationStatus status,
    svm::transport::SerialErrorCategory category = svm::transport::SerialErrorCategory::None) {
    return {
        .operation = {
            .requestId = requestId,
            .generation = generation,
            .kind = svm::transport::SerialOperationKind::Write,
        },
        .status = status,
        .error = {.category = category},
    };
}

svm::transport::SerialReadResult serialRead(
    svm::transport::SerialSessionGeneration generation,
    svm::transport::SerialOperationStatus status,
    std::vector<std::uint8_t> bytes = {}) {
    return {
        .operation = {
            .operation = {
                .requestId = 1,
                .generation = generation,
                .kind = svm::transport::SerialOperationKind::Read,
            },
            .status = status,
        },
        .bytes = std::move(bytes),
    };
}

void idleStateAllowsNewOwners() {
    NativeSerialIoState state;
    assert(state.isIdle());
    assert(!state.isBusy());
    assert(state.owner() == NativeSerialIoOwner::None);
    assert(state.allowsManualSend());
    assert(state.allowsFileSend());
    assert(state.allowsModbusScan());
    assert(state.allowsOwner(NativeSerialIoOwner::ManualSend));
    assert(state.allowsOwner(NativeSerialIoOwner::FileSend));
    assert(state.allowsOwner(NativeSerialIoOwner::ModbusScan));
    assert(!state.allowsOwner(NativeSerialIoOwner::None));
    assert(state.allowsSerialPoll());
    assert(state.allowsLineControl());
    assert(!state.shouldDeferDisconnect());
}

void manualSendIsExclusiveAndShortLived() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::ManualSend));
    assert(state.isBusy());
    assert(state.isOwnedBy(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsOwner(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(!state.allowsSerialPoll());
    assert(!state.allowsLineControl());
    assert(!state.release(NativeSerialIoOwner::FileSend));
    assert(state.release(NativeSerialIoOwner::ManualSend));
    assert(state.isIdle());
}

void fileSendOwnsWritesButKeepsPollingAllowed() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::FileSend));
    assert(state.isOwnedBy(NativeSerialIoOwner::FileSend));
    assert(!state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(state.allowsSerialPoll());
    assert(!state.allowsLineControl());
    assert(!state.shouldDeferDisconnect());
    assert(state.release(NativeSerialIoOwner::FileSend));
}

void modbusScanOwnsThePortUntilFinished() {
    NativeSerialIoState state;
    assert(state.tryAcquire(NativeSerialIoOwner::ModbusScan));
    assert(state.isOwnedBy(NativeSerialIoOwner::ModbusScan));
    assert(!state.tryAcquire(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(!state.allowsSerialPoll());
    assert(!state.allowsLineControl());
    assert(state.shouldDeferDisconnect());
    state.forceRelease();
    assert(state.isIdle());
}

void writeQueueStatusTracksPendingCapacityAndBackpressure() {
    NativeSerialIoState state;
    assert(state.writeQueueStatus().empty());
    assert(!state.writeQueueStatus().full());
    assert(!state.hasPendingSerialWrites());
    assert(!state.serialWriteQueueHasBackpressure());

    state.updateWriteQueueStatus(7, queueSnapshot(1, 0));
    assert(state.writeQueueStatus().generation == 7);
    assert(state.writeQueueStatus().pendingCount == 1);
    assert(state.writeQueueStatus().activeCount == 0);
    assert(state.writeQueueStatus().requestCapacity == 4);
    assert(state.writeQueueStatus().pendingBytes == 16);
    assert(state.writeQueueStatus().activeBytes == 0);
    assert(state.writeQueueStatus().byteCapacity == 1024);
    assert(state.writeQueueStatus().countedCount() == 1);
    assert(state.writeQueueStatus().countedBytes() == 16);
    assert(state.hasPendingSerialWrites());
    assert(!state.serialWriteQueueHasBackpressure());

    state.updateWriteQueueStatus(7, queueSnapshot(3, 1));
    assert(state.serialWriteQueueHasBackpressure());

    svm::transport::SerialWriteQueueSnapshot byteFull = queueSnapshot(1, 1, 8);
    byteFull.byteCapacity = byteFull.pendingBytes + byteFull.activeBytes;
    state.updateWriteQueueStatus(7, byteFull);
    assert(state.writeQueueStatus().countedCount() < state.writeQueueStatus().requestCapacity);
    assert(state.serialWriteQueueHasBackpressure());

    svm::transport::SerialWriteQueueSnapshot snapshot = queueSnapshot(2, 1, 8);
    state.updateWriteQueueStatus(8, snapshot);
    assert(state.writeQueueStatus().generation == 8);
    assert(state.writeQueueStatus().requestCapacity == 8);
    assert(state.writeQueueStatus().pendingCount == 2);
    assert(state.writeQueueStatus().activeCount == 1);
    assert(state.writeQueueStatus().countedCount() == 3);
    assert(!state.serialWriteQueueHasBackpressure());
}

void pendingWritesBlockExclusiveOwnersButAllowManualBacklog() {
    NativeSerialIoState state;
    state.updateWriteQueueStatus(7, queueSnapshot(1, 0));

    assert(state.hasPendingSerialWrites());
    assert(state.allowsManualSend());
    assert(state.allowsOwner(NativeSerialIoOwner::ManualSend));
    assert(!state.allowsFileSend());
    assert(!state.allowsOwner(NativeSerialIoOwner::FileSend));
    assert(!state.allowsModbusScan());
    assert(!state.allowsOwner(NativeSerialIoOwner::ModbusScan));
    assert(!state.allowsLineControl());
    assert(state.tryAcquire(NativeSerialIoOwner::ManualSend));
    assert(state.release(NativeSerialIoOwner::ManualSend));
}

void fullWriteQueueBlocksManualBacklogToo() {
    NativeSerialIoState state;
    state.updateWriteQueueStatus(7, queueSnapshot(3, 1));

    assert(state.serialWriteQueueHasBackpressure());
    assert(!state.allowsManualSend());
    assert(!state.allowsOwner(NativeSerialIoOwner::ManualSend));
    assert(!state.tryAcquire(NativeSerialIoOwner::ManualSend));
}

void activeWriteRemainsOutstandingUntilTerminalSnapshot() {
    NativeSerialIoState state;
    state.updateWriteQueueStatus(11, queueSnapshot(0, 1));

    assert(!state.writeQueueStatus().empty());
    assert(state.hasPendingSerialWrites());
    assert(state.allowsManualSend());
    assert(!state.allowsFileSend());
    assert(!state.allowsModbusScan());
    assert(!state.allowsLineControl());

    state.updateWriteQueueStatus(11, queueSnapshot(0, 0));
    assert(state.writeQueueStatus().empty());
    assert(!state.hasPendingSerialWrites());
    assert(state.allowsFileSend());
    assert(state.allowsModbusScan());
    assert(state.allowsLineControl());
}

void generationReplacementDropsOldQueueAccounting() {
    NativeSerialIoState state;
    state.updateWriteQueueStatus(21, queueSnapshot(1, 1));
    assert(state.hasPendingSerialWrites());

    state.updateWriteQueueStatus(22, queueSnapshot(0, 0));
    assert(state.writeQueueStatus().generation == 22);
    assert(state.writeQueueStatus().countedCount() == 0);
    assert(state.writeQueueStatus().countedBytes() == 0);
    assert(!state.serialWriteQueueHasBackpressure());
}

void writeKeysRequireGenerationAndRequestIdentity() {
    const NativeSerialWriteKey key{.generation = 41, .requestId = 9};
    const svm::transport::SerialOperationDescriptor matching{
        .requestId = 9,
        .generation = 41,
        .kind = svm::transport::SerialOperationKind::Write,
    };
    auto staleGeneration = matching;
    staleGeneration.generation = 42;
    auto differentRequest = matching;
    differentRequest.requestId = 10;

    assert(key.assigned());
    assert(key.matches(matching));
    assert(!key.matches(staleGeneration));
    assert(!key.matches(differentRequest));
    assert(!NativeSerialWriteKey{}.matches(matching));
}

void admissionCreatesKeysOnlyForAcceptedCurrentGeneration() {
    const svm::transport::SerialWriteAdmissionResult accepted{
        .operation = {
            .requestId = 12,
            .generation = 45,
            .kind = svm::transport::SerialOperationKind::Write,
        },
        .status = svm::transport::SerialOperationStatus::Accepted,
    };
    auto rejected = accepted;
    rejected.status = svm::transport::SerialOperationStatus::RejectedFull;

    const NativeSerialWriteKey key = svm::win32::nativeSerialWriteKeyForAdmission(accepted, 45);
    assert(key.assigned());
    assert(key.generation == 45);
    assert(key.requestId == 12);
    assert(!svm::win32::nativeSerialWriteKeyForAdmission(rejected, 45).assigned());
    assert(!svm::win32::nativeSerialWriteKeyForAdmission(accepted, 46).assigned());
}

void directCancellationMatchesOnlyReturnedCurrentKey() {
    const NativeSerialWriteKey key{.generation = 47, .requestId = 13};
    const auto cancelled = terminalWrite(
        47,
        13,
        svm::transport::SerialOperationStatus::Cancelled,
        svm::transport::SerialErrorCategory::Cancelled);
    const auto succeeded = terminalWrite(47, 13, svm::transport::SerialOperationStatus::Succeeded);

    assert(svm::win32::nativeSerialWriteCancellationMatches(key, cancelled, 47));
    assert(!svm::win32::nativeSerialWriteCancellationMatches(key, cancelled, 48));
    assert(!svm::win32::nativeSerialWriteCancellationMatches(key, succeeded, 47));
}

void completionDecisionsConsumeExactPairsOnce() {
    const NativeSerialWriteKey oldKey{.generation = 51, .requestId = 4};
    const NativeSerialWriteKey currentKey{.generation = 52, .requestId = 4};
    std::deque<NativeSerialWriteKey> pending{oldKey, currentKey};
    const auto consume = [&pending](
                             const svm::transport::SerialTerminalResult& result,
                             const svm::transport::SerialSessionSnapshot& session) {
        if (!result.terminal()
            || !result.operation.assigned()
            || result.operation.kind != svm::transport::SerialOperationKind::Write) {
            return NativeSerialWriteCompletionDecision::Ignore;
        }
        const auto found = std::find_if(
            pending.begin(),
            pending.end(),
            [&result](const NativeSerialWriteKey& key) {
                return key.matches(result.operation);
            });
        if (found == pending.end()) {
            return NativeSerialWriteCompletionDecision::Ignore;
        }
        const NativeSerialWriteCompletionDecision decision =
            svm::win32::nativeSerialWriteCompletionDecision(
                *found,
                result,
                session,
                session.generation);
        pending.erase(found);
        return decision;
    };

    const auto oldSuccess = terminalWrite(51, 4, svm::transport::SerialOperationStatus::Succeeded);
    const auto currentSuccess = terminalWrite(52, 4, svm::transport::SerialOperationStatus::Succeeded);
    const auto nonTerminal = terminalWrite(51, 4, svm::transport::SerialOperationStatus::RejectedFull);
    const svm::transport::SerialSessionSnapshot currentSession = openSession(52);
    assert(consume(nonTerminal, currentSession) == NativeSerialWriteCompletionDecision::Ignore);
    assert(pending.size() == 2);
    assert(consume(oldSuccess, currentSession) == NativeSerialWriteCompletionDecision::Ignore);
    assert(pending.size() == 1);
    assert(pending.front().generation == 52);
    assert(consume(oldSuccess, currentSession) == NativeSerialWriteCompletionDecision::Ignore);
    assert(pending.size() == 1);
    assert(consume(currentSuccess, currentSession) == NativeSerialWriteCompletionDecision::Succeeded);
    assert(pending.empty());
}

void completionDecisionsPreserveOnlyCurrentFaultEvidence() {
    const NativeSerialWriteKey key{.generation = 61, .requestId = 7};
    const svm::transport::SerialSessionSnapshot currentSession = openSession(61);
    const auto cancelled = terminalWrite(61, 7, svm::transport::SerialOperationStatus::Cancelled);
    const auto failed = terminalWrite(61, 7, svm::transport::SerialOperationStatus::Timeout);
    const auto rejected = terminalWrite(61, 7, svm::transport::SerialOperationStatus::RejectedFull);
    assert(svm::win32::nativeSerialWriteCompletionDecision(key, cancelled, currentSession, 61)
        == NativeSerialWriteCompletionDecision::Cancelled);
    assert(svm::win32::nativeSerialWriteCompletionDecision(key, failed, currentSession, 61)
        == NativeSerialWriteCompletionDecision::Failed);
    assert(svm::win32::nativeSerialWriteCompletionDecision(key, rejected, currentSession, 61)
        == NativeSerialWriteCompletionDecision::Ignore);

    svm::transport::SerialSessionSnapshot faulted;
    faulted.state = svm::transport::SerialSessionState::Faulted;
    const auto disconnected = terminalWrite(
        61,
        7,
        svm::transport::SerialOperationStatus::Disconnected,
        svm::transport::SerialErrorCategory::Disconnected);
    assert(svm::win32::nativeSerialWriteCompletionDecision(key, disconnected, faulted, 61)
        == NativeSerialWriteCompletionDecision::Failed);
    assert(svm::win32::nativeSerialWriteCompletionDecision(key, disconnected, faulted, 62)
        == NativeSerialWriteCompletionDecision::Ignore);
    assert(svm::win32::nativeSerialWriteCompletionDecision(key, cancelled, faulted, 61)
        == NativeSerialWriteCompletionDecision::Ignore);
}

void readDecisionsCoverEmptyFailureAndStaleResults() {
    assert(svm::win32::nativeSerialReadDecision(
               serialRead(71, svm::transport::SerialOperationStatus::Succeeded, {0x01}),
               71)
        == NativeSerialReadDecision::Append);
    assert(svm::win32::nativeSerialReadDecision(
               serialRead(71, svm::transport::SerialOperationStatus::Succeeded),
               71)
        == NativeSerialReadDecision::Stop);
    assert(svm::win32::nativeSerialReadDecision(
               serialRead(71, svm::transport::SerialOperationStatus::Timeout),
               71)
        == NativeSerialReadDecision::Stop);
    assert(svm::win32::nativeSerialReadDecision(
               serialRead(71, svm::transport::SerialOperationStatus::Disconnected),
               71)
        == NativeSerialReadDecision::Fail);
    assert(svm::win32::nativeSerialReadDecision(
               serialRead(71, svm::transport::SerialOperationStatus::RejectedInvalid),
               71)
        == NativeSerialReadDecision::ReportError);
    assert(svm::win32::nativeSerialReadDecision(
               serialRead(70, svm::transport::SerialOperationStatus::Succeeded, {0x02}),
               71)
        == NativeSerialReadDecision::Stop);
    auto wrongKind = serialRead(71, svm::transport::SerialOperationStatus::Succeeded, {0x03});
    wrongKind.operation.operation.kind = svm::transport::SerialOperationKind::Write;
    assert(svm::win32::nativeSerialReadDecision(wrongKind, 71)
        == NativeSerialReadDecision::ReportError);
}

} // namespace

int main() {
    idleStateAllowsNewOwners();
    manualSendIsExclusiveAndShortLived();
    fileSendOwnsWritesButKeepsPollingAllowed();
    modbusScanOwnsThePortUntilFinished();
    writeQueueStatusTracksPendingCapacityAndBackpressure();
    pendingWritesBlockExclusiveOwnersButAllowManualBacklog();
    fullWriteQueueBlocksManualBacklogToo();
    activeWriteRemainsOutstandingUntilTerminalSnapshot();
    generationReplacementDropsOldQueueAccounting();
    writeKeysRequireGenerationAndRequestIdentity();
    admissionCreatesKeysOnlyForAcceptedCurrentGeneration();
    directCancellationMatchesOnlyReturnedCurrentKey();
    completionDecisionsConsumeExactPairsOnce();
    completionDecisionsPreserveOnlyCurrentFaultEvidence();
    readDecisionsCoverEmptyFailureAndStaleResults();

    std::cout << "native_serial_io_state_tests passed\n";
    return 0;
}
