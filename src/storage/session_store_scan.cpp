#include "storage/session_store.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>


namespace svm::storage {

bool SessionStore::saveScanExecution(const ScanExecutionPersistenceRecord& execution) {
    if (execution.session.sessionId.isEmpty()) {
        m_lastErrorText = QStringLiteral("保存扫描结果失败：扫描会话 ID 不能为空。");
        return false;
    }

    if (!m_db.transaction()) {
        m_lastErrorText = QStringLiteral("保存扫描结果失败：无法开启数据库事务：%1").arg(m_db.lastError().text());
        return false;
    }

    auto rollback = [this](const QString& message) {
        m_lastErrorText = message;
        m_db.rollback();
        return false;
    };

    QSqlQuery deleteObservations(m_db);
    deleteObservations.prepare(QStringLiteral("DELETE FROM scan_observations WHERE session_id = :session_id"));
    deleteObservations.bindValue(QStringLiteral(":session_id"), execution.session.sessionId);
    if (!deleteObservations.exec()) {
        return rollback(QStringLiteral("保存扫描结果失败：清理旧观测失败：%1").arg(deleteObservations.lastError().text()));
    }

    QSqlQuery deleteAttempts(m_db);
    deleteAttempts.prepare(QStringLiteral("DELETE FROM scan_attempts WHERE session_id = :session_id"));
    deleteAttempts.bindValue(QStringLiteral(":session_id"), execution.session.sessionId);
    if (!deleteAttempts.exec()) {
        return rollback(QStringLiteral("保存扫描结果失败：清理旧尝试记录失败：%1").arg(deleteAttempts.lastError().text()));
    }

    QSqlQuery sessionQuery(m_db);
    sessionQuery.prepare(QStringLiteral(R"sql(
        INSERT INTO scan_sessions(
            session_id, slave_id, function_code, start_address, end_address, block_size, request_count,
            status, started_at_utc, finished_at_utc, success_block_count, failed_block_count, error_message
        ) VALUES (
            :session_id, :slave_id, :function_code, :start_address, :end_address, :block_size, :request_count,
            :status, :started_at_utc, :finished_at_utc, :success_block_count, :failed_block_count, :error_message
        )
        ON CONFLICT(session_id) DO UPDATE SET
            slave_id = excluded.slave_id,
            function_code = excluded.function_code,
            start_address = excluded.start_address,
            end_address = excluded.end_address,
            block_size = excluded.block_size,
            request_count = excluded.request_count,
            status = excluded.status,
            started_at_utc = excluded.started_at_utc,
            finished_at_utc = excluded.finished_at_utc,
            success_block_count = excluded.success_block_count,
            failed_block_count = excluded.failed_block_count,
            error_message = excluded.error_message
    )sql"));
    sessionQuery.bindValue(QStringLiteral(":session_id"), notNullString(execution.session.sessionId));
    sessionQuery.bindValue(QStringLiteral(":slave_id"), execution.session.slaveId);
    sessionQuery.bindValue(QStringLiteral(":function_code"), execution.session.functionCode);
    sessionQuery.bindValue(QStringLiteral(":start_address"), execution.session.startAddress);
    sessionQuery.bindValue(QStringLiteral(":end_address"), execution.session.endAddress);
    sessionQuery.bindValue(QStringLiteral(":block_size"), execution.session.blockSize);
    sessionQuery.bindValue(QStringLiteral(":request_count"), execution.session.requestCount);
    sessionQuery.bindValue(QStringLiteral(":status"), notNullString(execution.session.status));
    sessionQuery.bindValue(QStringLiteral(":started_at_utc"), dateToString(execution.session.startedAtUtc));
    sessionQuery.bindValue(QStringLiteral(":finished_at_utc"), dateToString(execution.session.finishedAtUtc));
    sessionQuery.bindValue(QStringLiteral(":success_block_count"), execution.session.successBlockCount);
    sessionQuery.bindValue(QStringLiteral(":failed_block_count"), execution.session.failedBlockCount);
    sessionQuery.bindValue(QStringLiteral(":error_message"), notNullString(execution.session.errorMessage));
    if (!sessionQuery.exec()) {
        return rollback(QStringLiteral("保存扫描会话失败：%1").arg(sessionQuery.lastError().text()));
    }

    for (const auto& attempt : execution.attempts) {
        QSqlQuery attemptQuery(m_db);
        attemptQuery.prepare(QStringLiteral(R"sql(
            INSERT INTO scan_attempts(
                session_id, block_index, attempt_index, start_address, quantity, status,
                request_frame, response_frame, error_message, is_modbus_exception, exception_code,
                exception_description, sent_at_utc, received_at_utc, endpoint
            ) VALUES (
                :session_id, :block_index, :attempt_index, :start_address, :quantity, :status,
                :request_frame, :response_frame, :error_message, :is_modbus_exception, :exception_code,
                :exception_description, :sent_at_utc, :received_at_utc, :endpoint
            )
        )sql"));
        attemptQuery.bindValue(QStringLiteral(":session_id"), notNullString(attempt.sessionId.isEmpty() ? execution.session.sessionId : attempt.sessionId));
        attemptQuery.bindValue(QStringLiteral(":block_index"), attempt.blockIndex);
        attemptQuery.bindValue(QStringLiteral(":attempt_index"), attempt.attemptIndex);
        attemptQuery.bindValue(QStringLiteral(":start_address"), attempt.startAddress);
        attemptQuery.bindValue(QStringLiteral(":quantity"), attempt.quantity);
        attemptQuery.bindValue(QStringLiteral(":status"), notNullString(attempt.status));
        attemptQuery.bindValue(QStringLiteral(":request_frame"), attempt.requestFrame);
        attemptQuery.bindValue(QStringLiteral(":response_frame"), attempt.responseFrame);
        attemptQuery.bindValue(QStringLiteral(":error_message"), notNullString(attempt.errorMessage));
        attemptQuery.bindValue(QStringLiteral(":is_modbus_exception"), attempt.isModbusException ? 1 : 0);
        attemptQuery.bindValue(QStringLiteral(":exception_code"), attempt.exceptionCode);
        attemptQuery.bindValue(QStringLiteral(":exception_description"), notNullString(attempt.exceptionDescription));
        attemptQuery.bindValue(QStringLiteral(":sent_at_utc"), dateToString(attempt.sentAtUtc));
        attemptQuery.bindValue(QStringLiteral(":received_at_utc"), dateToString(attempt.receivedAtUtc));
        attemptQuery.bindValue(QStringLiteral(":endpoint"), notNullString(attempt.endpoint));
        if (!attemptQuery.exec()) {
            return rollback(QStringLiteral("保存扫描请求尝试失败：%1").arg(attemptQuery.lastError().text()));
        }
    }

    for (const auto& observation : execution.observations) {
        QSqlQuery observationQuery(m_db);
        observationQuery.prepare(QStringLiteral(R"sql(
            INSERT INTO scan_observations(
                session_id, block_index, attempt_index, slave_id, function_code, address, value, observed_at_utc
            ) VALUES (
                :session_id, :block_index, :attempt_index, :slave_id, :function_code, :address, :value, :observed_at_utc
            )
        )sql"));
        observationQuery.bindValue(QStringLiteral(":session_id"), notNullString(observation.sessionId.isEmpty() ? execution.session.sessionId : observation.sessionId));
        observationQuery.bindValue(QStringLiteral(":block_index"), observation.blockIndex);
        observationQuery.bindValue(QStringLiteral(":attempt_index"), observation.attemptIndex);
        observationQuery.bindValue(QStringLiteral(":slave_id"), observation.slaveId);
        observationQuery.bindValue(QStringLiteral(":function_code"), observation.functionCode);
        observationQuery.bindValue(QStringLiteral(":address"), observation.address);
        observationQuery.bindValue(QStringLiteral(":value"), observation.value);
        observationQuery.bindValue(QStringLiteral(":observed_at_utc"), dateToString(observation.observedAtUtc));
        if (!observationQuery.exec()) {
            return rollback(QStringLiteral("保存寄存器观测失败：%1").arg(observationQuery.lastError().text()));
        }
    }

    if (!m_db.commit()) {
        return rollback(QStringLiteral("保存扫描结果失败：提交事务失败：%1").arg(m_db.lastError().text()));
    }

    return true;
}

std::optional<ScanSessionRecord> SessionStore::scanSession(const QString& sessionId) const {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        SELECT session_id, slave_id, function_code, start_address, end_address, block_size, request_count,
               status, started_at_utc, finished_at_utc, success_block_count, failed_block_count, error_message
        FROM scan_sessions
        WHERE session_id = :session_id
    )sql"));
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }

    ScanSessionRecord record;
    record.sessionId = query.value(0).toString();
    record.slaveId = query.value(1).toInt();
    record.functionCode = query.value(2).toInt();
    record.startAddress = query.value(3).toInt();
    record.endAddress = query.value(4).toInt();
    record.blockSize = query.value(5).toInt();
    record.requestCount = query.value(6).toInt();
    record.status = query.value(7).toString();
    record.startedAtUtc = dateFromString(query.value(8).toString());
    record.finishedAtUtc = dateFromString(query.value(9).toString());
    record.successBlockCount = query.value(10).toInt();
    record.failedBlockCount = query.value(11).toInt();
    record.errorMessage = query.value(12).toString();
    return record;
}

std::optional<ScanSessionRecord> SessionStore::latestScanSession() const {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT session_id, slave_id, function_code, start_address, end_address, block_size, request_count,
               status, started_at_utc, finished_at_utc, success_block_count, failed_block_count, error_message
        FROM scan_sessions
        ORDER BY finished_at_utc DESC, started_at_utc DESC, session_id DESC
        LIMIT 1
    )sql"))) {
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    ScanSessionRecord record;
    record.sessionId = query.value(0).toString();
    record.slaveId = query.value(1).toInt();
    record.functionCode = query.value(2).toInt();
    record.startAddress = query.value(3).toInt();
    record.endAddress = query.value(4).toInt();
    record.blockSize = query.value(5).toInt();
    record.requestCount = query.value(6).toInt();
    record.status = query.value(7).toString();
    record.startedAtUtc = dateFromString(query.value(8).toString());
    record.finishedAtUtc = dateFromString(query.value(9).toString());
    record.successBlockCount = query.value(10).toInt();
    record.failedBlockCount = query.value(11).toInt();
    record.errorMessage = query.value(12).toString();
    return record;
}

QList<ScanSessionRecord> SessionStore::recentScanSessions(int limit) const {
    QList<ScanSessionRecord> sessions;
    const int safeLimit = qMax(1, limit);
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT session_id, slave_id, function_code, start_address, end_address, block_size, request_count,
               status, started_at_utc, finished_at_utc, success_block_count, failed_block_count, error_message
        FROM scan_sessions
        ORDER BY finished_at_utc DESC, started_at_utc DESC, session_id DESC
        LIMIT %1
    )sql").arg(safeLimit))) {
        return sessions;
    }

    while (query.next()) {
        ScanSessionRecord record;
        record.sessionId = query.value(0).toString();
        record.slaveId = query.value(1).toInt();
        record.functionCode = query.value(2).toInt();
        record.startAddress = query.value(3).toInt();
        record.endAddress = query.value(4).toInt();
        record.blockSize = query.value(5).toInt();
        record.requestCount = query.value(6).toInt();
        record.status = query.value(7).toString();
        record.startedAtUtc = dateFromString(query.value(8).toString());
        record.finishedAtUtc = dateFromString(query.value(9).toString());
        record.successBlockCount = query.value(10).toInt();
        record.failedBlockCount = query.value(11).toInt();
        record.errorMessage = query.value(12).toString();
        sessions.append(record);
    }
    return sessions;
}

QList<ScanAttemptRecord> SessionStore::scanAttempts(const QString& sessionId) const {
    clearReadError();
    QList<ScanAttemptRecord> attempts;
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT id, session_id, block_index, attempt_index, start_address, quantity, status,
               request_frame, response_frame, error_message, is_modbus_exception, exception_code,
               exception_description, sent_at_utc, received_at_utc, endpoint
        FROM scan_attempts
        WHERE session_id = :session_id
        ORDER BY block_index ASC, attempt_index ASC, id ASC
    )sql"))) {
        setReadError(QStringLiteral("读取扫描请求尝试失败"), query.lastError());
        return attempts;
    }
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取扫描请求尝试失败"), query.lastError());
        return attempts;
    }

    while (query.next()) {
        ScanAttemptRecord record;
        record.id = query.value(0).toLongLong();
        record.sessionId = query.value(1).toString();
        record.blockIndex = query.value(2).toInt();
        record.attemptIndex = query.value(3).toInt();
        record.startAddress = query.value(4).toInt();
        record.quantity = query.value(5).toInt();
        record.status = query.value(6).toString();
        record.requestFrame = query.value(7).toByteArray();
        record.responseFrame = query.value(8).toByteArray();
        record.errorMessage = query.value(9).toString();
        record.isModbusException = query.value(10).toInt() != 0;
        record.exceptionCode = query.value(11).toInt();
        record.exceptionDescription = query.value(12).toString();
        record.sentAtUtc = dateFromString(query.value(13).toString());
        record.receivedAtUtc = dateFromString(query.value(14).toString());
        record.endpoint = query.value(15).toString();
        attempts.append(record);
    }
    return attempts;
}

QList<ScanObservationRecord> SessionStore::scanObservations(const QString& sessionId) const {
    clearReadError();
    QList<ScanObservationRecord> observations;
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT id, session_id, block_index, attempt_index, slave_id, function_code, address, value, observed_at_utc
        FROM scan_observations
        WHERE session_id = :session_id
        ORDER BY address ASC, id ASC
    )sql"))) {
        setReadError(QStringLiteral("读取扫描观测失败"), query.lastError());
        return observations;
    }
    query.bindValue(QStringLiteral(":session_id"), sessionId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取扫描观测失败"), query.lastError());
        return observations;
    }

    while (query.next()) {
        ScanObservationRecord record;
        record.id = query.value(0).toLongLong();
        record.sessionId = query.value(1).toString();
        record.blockIndex = query.value(2).toInt();
        record.attemptIndex = query.value(3).toInt();
        record.slaveId = query.value(4).toInt();
        record.functionCode = query.value(5).toInt();
        record.address = query.value(6).toInt();
        record.value = query.value(7).toInt();
        record.observedAtUtc = dateFromString(query.value(8).toString());
        observations.append(record);
    }
    return observations;
}

QList<ScanObservationRecord> SessionStore::scanObservationsByIds(const QList<qint64>& observationIds) const {
    clearReadError();
    QList<ScanObservationRecord> observations;
    if (observationIds.isEmpty()) {
        return observations;
    }

    QStringList placeholders;
    placeholders.reserve(observationIds.size());
    for (int index = 0; index < observationIds.size(); ++index) {
        placeholders.append(QStringLiteral(":id_%1").arg(index));
    }

    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT id, session_id, block_index, attempt_index, slave_id, function_code, address, value, observed_at_utc
        FROM scan_observations
        WHERE id IN (%1)
        ORDER BY id ASC
    )sql").arg(placeholders.join(QStringLiteral(", "))))) {
        setReadError(QStringLiteral("按 ID 读取扫描观测失败"), query.lastError());
        return observations;
    }
    for (int index = 0; index < observationIds.size(); ++index) {
        query.bindValue(QStringLiteral(":id_%1").arg(index), observationIds.at(index));
    }
    if (!query.exec()) {
        setReadError(QStringLiteral("按 ID 读取扫描观测失败"), query.lastError());
        return observations;
    }

    while (query.next()) {
        ScanObservationRecord record;
        record.id = query.value(0).toLongLong();
        record.sessionId = query.value(1).toString();
        record.blockIndex = query.value(2).toInt();
        record.attemptIndex = query.value(3).toInt();
        record.slaveId = query.value(4).toInt();
        record.functionCode = query.value(5).toInt();
        record.address = query.value(6).toInt();
        record.value = query.value(7).toInt();
        record.observedAtUtc = dateFromString(query.value(8).toString());
        observations.append(record);
    }
    return observations;
}

} // namespace svm::storage
