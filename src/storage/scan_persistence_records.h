#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace svm::storage {

struct ScanSessionRecord {
    QString sessionId;
    int slaveId = 0;
    int functionCode = 0;
    int startAddress = 0;
    int endAddress = 0;
    int blockSize = 0;
    int requestCount = 0;
    QString status;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    int successBlockCount = 0;
    int failedBlockCount = 0;
    QString errorMessage;
};

struct ScanAttemptRecord {
    qint64 id = 0;
    QString sessionId;
    int blockIndex = -1;
    int attemptIndex = 0;
    int startAddress = 0;
    int quantity = 0;
    QString status;
    QByteArray requestFrame;
    QByteArray responseFrame;
    QString errorMessage;
    bool isModbusException = false;
    int exceptionCode = 0;
    QString exceptionDescription;
    QDateTime sentAtUtc;
    QDateTime receivedAtUtc;
    QString endpoint;
};

struct ScanObservationRecord {
    qint64 id = 0;
    QString sessionId;
    int blockIndex = -1;
    int attemptIndex = 0;
    int slaveId = 0;
    int functionCode = 0;
    int address = 0;
    int value = 0;
    QDateTime observedAtUtc;
};

struct ScanExecutionPersistenceRecord {
    ScanSessionRecord session;
    QList<ScanAttemptRecord> attempts;
    QList<ScanObservationRecord> observations;
};

} // namespace svm::storage
