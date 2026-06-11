#include "storage/session_store.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>


namespace svm::storage {

namespace {

QString quotedSqlIdentifier(QString identifier) {
    identifier.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(identifier);
}

bool isAllowedTextColumnMigration(const QString& tableName, const QString& columnName) {
    return (tableName == QStringLiteral("protocol_field_rules") && columnName == QStringLiteral("interpretation_map"))
        || (tableName == QStringLiteral("rule_verification_results") && columnName == QStringLiteral("interpretation_text"))
        || (tableName == QStringLiteral("rule_verification_results") && columnName == QStringLiteral("candidate_type"));
}

bool tableColumnExists(QSqlDatabase& db, const QString& tableName, const QString& columnName, QString* errorText) {
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(quotedSqlIdentifier(tableName)))) {
        if (errorText != nullptr) {
            *errorText = query.lastError().text();
        }
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }
    return false;
}

bool ensureTextColumn(QSqlDatabase& db, const QString& tableName, const QString& columnName, QString* errorText) {
    if (!isAllowedTextColumnMigration(tableName, columnName)) {
        if (errorText != nullptr) {
            *errorText = QStringLiteral("拒绝未登记的数据库结构迁移：%1.%2").arg(tableName, columnName);
        }
        return false;
    }

    if (tableColumnExists(db, tableName, columnName, errorText)) {
        return true;
    }
    if (errorText != nullptr && !errorText->isEmpty()) {
        return false;
    }

    QSqlQuery query(db);
    const QString sql = QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 TEXT NOT NULL DEFAULT ''")
        .arg(quotedSqlIdentifier(tableName), quotedSqlIdentifier(columnName));
    if (!query.exec(sql)) {
        if (errorText != nullptr) {
            *errorText = query.lastError().text();
        }
        return false;
    }
    return true;
}

} // namespace

bool SessionStore::open(const QString& databasePath) {
    closeDatabaseConnection();

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(databasePath);

    if (!m_db.open()) {
        m_lastErrorText = QStringLiteral("无法打开 SQLite 数据库：%1").arg(m_db.lastError().text());
        return false;
    }

    return initializeSchema();
}

bool SessionStore::initializeSchema() {
    QSqlQuery query(m_db);
    const QStringList statements = {
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS raw_io_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                direction TEXT NOT NULL,
                timestamp_utc TEXT NOT NULL,
                endpoint TEXT NOT NULL,
                payload BLOB NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS send_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                content TEXT NOT NULL,
                payload_mode INTEGER NOT NULL,
                line_ending INTEGER NOT NULL,
                sent_at_utc TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE UNIQUE INDEX IF NOT EXISTS ux_send_history_identity
            ON send_history(content, payload_mode, line_ending)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS serial_profiles (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                port_name TEXT NOT NULL,
                baud_rate INTEGER NOT NULL,
                data_bits INTEGER NOT NULL,
                parity INTEGER NOT NULL,
                stop_bits INTEGER NOT NULL,
                flow_control INTEGER NOT NULL,
                dtr INTEGER NOT NULL,
                rts INTEGER NOT NULL,
                updated_at_utc TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS scan_sessions (
                session_id TEXT PRIMARY KEY,
                slave_id INTEGER NOT NULL,
                function_code INTEGER NOT NULL,
                start_address INTEGER NOT NULL,
                end_address INTEGER NOT NULL,
                block_size INTEGER NOT NULL,
                request_count INTEGER NOT NULL,
                status TEXT NOT NULL,
                started_at_utc TEXT NOT NULL,
                finished_at_utc TEXT NOT NULL,
                success_block_count INTEGER NOT NULL,
                failed_block_count INTEGER NOT NULL,
                error_message TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS scan_attempts (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                block_index INTEGER NOT NULL,
                attempt_index INTEGER NOT NULL,
                start_address INTEGER NOT NULL,
                quantity INTEGER NOT NULL,
                status TEXT NOT NULL,
                request_frame BLOB NOT NULL,
                response_frame BLOB NOT NULL,
                error_message TEXT NOT NULL,
                is_modbus_exception INTEGER NOT NULL,
                exception_code INTEGER NOT NULL,
                exception_description TEXT NOT NULL,
                sent_at_utc TEXT NOT NULL,
                received_at_utc TEXT NOT NULL,
                endpoint TEXT NOT NULL,
                FOREIGN KEY(session_id) REFERENCES scan_sessions(session_id)
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE UNIQUE INDEX IF NOT EXISTS ux_scan_attempt_identity
            ON scan_attempts(session_id, block_index, attempt_index)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS scan_observations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                block_index INTEGER NOT NULL,
                attempt_index INTEGER NOT NULL,
                slave_id INTEGER NOT NULL,
                function_code INTEGER NOT NULL,
                address INTEGER NOT NULL,
                value INTEGER NOT NULL,
                observed_at_utc TEXT NOT NULL,
                FOREIGN KEY(session_id) REFERENCES scan_sessions(session_id)
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE INDEX IF NOT EXISTS ix_scan_observations_session_address
            ON scan_observations(session_id, address)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS match_runs (
                run_id TEXT PRIMARY KEY,
                source_scan_session_id TEXT NOT NULL,
                target_label TEXT NOT NULL,
                target_value REAL NOT NULL,
                target_unit TEXT NOT NULL,
                sampled_at_utc TEXT NOT NULL,
                tolerance_absolute REAL NOT NULL,
                tolerance_relative_ratio REAL NOT NULL,
                candidate_count INTEGER NOT NULL,
                created_at_utc TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS match_candidates (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                run_id TEXT NOT NULL,
                rank_index INTEGER NOT NULL,
                candidate_type TEXT NOT NULL,
                word_order TEXT NOT NULL,
                byte_order TEXT NOT NULL,
                source_session_id TEXT NOT NULL,
                slave_id INTEGER NOT NULL,
                function_code INTEGER NOT NULL,
                start_address INTEGER NOT NULL,
                register_count INTEGER NOT NULL,
                observation_ids TEXT NOT NULL,
                addresses TEXT NOT NULL,
                block_indexes TEXT NOT NULL,
                attempt_indexes TEXT NOT NULL,
                raw_registers TEXT NOT NULL,
                decoded_value REAL NOT NULL,
                scale_multiplier REAL NOT NULL,
                scale_offset REAL NOT NULL,
                engineering_value REAL NOT NULL,
                delta REAL NOT NULL,
                absolute_error REAL NOT NULL,
                effective_tolerance REAL NOT NULL,
                score REAL NOT NULL,
                observed_at_utc TEXT NOT NULL,
                evidence_text TEXT NOT NULL,
                FOREIGN KEY(run_id) REFERENCES match_runs(run_id)
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE UNIQUE INDEX IF NOT EXISTS ux_match_candidates_rank
            ON match_candidates(run_id, rank_index)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS stability_runs (
                stability_run_id TEXT PRIMARY KEY,
                source_match_run_ids TEXT NOT NULL,
                minimum_sample_count INTEGER NOT NULL,
                strong_sample_count INTEGER NOT NULL,
                stable_candidate_count INTEGER NOT NULL,
                created_at_utc TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS stable_candidates (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                stability_run_id TEXT NOT NULL,
                rank_index INTEGER NOT NULL,
                candidate_type TEXT NOT NULL,
                word_order TEXT NOT NULL,
                byte_order TEXT NOT NULL,
                slave_id INTEGER NOT NULL,
                function_code INTEGER NOT NULL,
                start_address INTEGER NOT NULL,
                register_count INTEGER NOT NULL,
                scale_multiplier REAL NOT NULL,
                scale_offset REAL NOT NULL,
                sample_count INTEGER NOT NULL,
                meets_minimum_sample_count INTEGER NOT NULL,
                confidence_level TEXT NOT NULL,
                run_ids TEXT NOT NULL,
                source_scan_session_ids TEXT NOT NULL,
                observation_ids TEXT NOT NULL,
                addresses TEXT NOT NULL,
                mean_target_value REAL NOT NULL,
                mean_engineering_value REAL NOT NULL,
                mean_absolute_error REAL NOT NULL,
                max_absolute_error REAL NOT NULL,
                mean_candidate_score REAL NOT NULL,
                mean_error_quality REAL NOT NULL,
                sample_quality REAL NOT NULL,
                stability_score REAL NOT NULL,
                evidence_summary TEXT NOT NULL,
                FOREIGN KEY(stability_run_id) REFERENCES stability_runs(stability_run_id)
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE UNIQUE INDEX IF NOT EXISTS ux_stable_candidates_rank
            ON stable_candidates(stability_run_id, rank_index)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS protocol_field_rules (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                rule_id TEXT NOT NULL UNIQUE,
                field_name TEXT NOT NULL,
                source_stability_run_id TEXT NOT NULL,
                source_stable_candidate_id INTEGER NOT NULL,
                candidate_type TEXT NOT NULL,
                word_order TEXT NOT NULL,
                byte_order TEXT NOT NULL,
                slave_id INTEGER NOT NULL,
                function_code INTEGER NOT NULL,
                start_address INTEGER NOT NULL,
                register_count INTEGER NOT NULL,
                scale_multiplier REAL NOT NULL,
                scale_offset REAL NOT NULL,
                unit TEXT NOT NULL,
                confidence_level TEXT NOT NULL,
                stability_score REAL NOT NULL,
                evidence_summary TEXT NOT NULL,
                interpretation_map TEXT NOT NULL DEFAULT '',
                created_at_utc TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE INDEX IF NOT EXISTS ix_protocol_field_rules_created
            ON protocol_field_rules(created_at_utc DESC, id DESC)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS rule_verification_runs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                verification_run_id TEXT NOT NULL UNIQUE,
                source_scan_session_id TEXT NOT NULL,
                rule_count INTEGER NOT NULL,
                verified_count INTEGER NOT NULL,
                missing_count INTEGER NOT NULL,
                unsupported_count INTEGER NOT NULL,
                created_at_utc TEXT NOT NULL
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE INDEX IF NOT EXISTS ix_rule_verification_runs_created
            ON rule_verification_runs(created_at_utc DESC, id DESC)
        )sql"),
        QStringLiteral(R"sql(
            CREATE TABLE IF NOT EXISTS rule_verification_results (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                verification_run_id TEXT NOT NULL,
                rule_id TEXT NOT NULL,
                field_name TEXT NOT NULL,
                unit TEXT NOT NULL,
                candidate_type TEXT NOT NULL DEFAULT '',
                source_scan_session_id TEXT NOT NULL,
                verified INTEGER NOT NULL,
                status_text TEXT NOT NULL,
                slave_id INTEGER NOT NULL,
                function_code INTEGER NOT NULL,
                start_address INTEGER NOT NULL,
                register_count INTEGER NOT NULL,
                observation_ids TEXT NOT NULL,
                raw_registers TEXT NOT NULL,
                decoded_value REAL NOT NULL,
                engineering_value REAL NOT NULL,
                observed_at_utc TEXT NOT NULL,
                interpretation_text TEXT NOT NULL DEFAULT '',
                evidence_text TEXT NOT NULL,
                FOREIGN KEY(verification_run_id) REFERENCES rule_verification_runs(verification_run_id)
            )
        )sql"),
        QStringLiteral(R"sql(
            CREATE INDEX IF NOT EXISTS ix_rule_verification_results_run
            ON rule_verification_results(verification_run_id, id)
        )sql")
    };

    for (const auto& statement : statements) {
        if (!query.exec(statement)) {
            m_lastErrorText = QStringLiteral("初始化数据库结构失败：%1").arg(query.lastError().text());
            return false;
        }
    }

    QString migrationError;
    if (!ensureTextColumn(m_db, QStringLiteral("protocol_field_rules"), QStringLiteral("interpretation_map"), &migrationError)) {
        m_lastErrorText = QStringLiteral("升级协议字段规则结构失败：%1").arg(migrationError);
        return false;
    }
    if (!ensureTextColumn(m_db, QStringLiteral("rule_verification_results"), QStringLiteral("interpretation_text"), &migrationError)) {
        m_lastErrorText = QStringLiteral("升级规则验证结果结构失败：%1").arg(migrationError);
        return false;
    }
    if (!ensureTextColumn(m_db, QStringLiteral("rule_verification_results"), QStringLiteral("candidate_type"), &migrationError)) {
        m_lastErrorText = QStringLiteral("升级规则验证结果类型字段失败：%1").arg(migrationError);
        return false;
    }
    return true;
}

bool SessionStore::appendRawEvent(const capture::RawIoEvent& event) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        INSERT INTO raw_io_events(session_id, direction, timestamp_utc, endpoint, payload)
        VALUES(:session_id, :direction, :timestamp_utc, :endpoint, :payload)
    )sql"));
    query.bindValue(QStringLiteral(":session_id"), notNullString(event.sessionId));
    query.bindValue(QStringLiteral(":direction"), event.direction == capture::Direction::Rx ? QStringLiteral("RX") : QStringLiteral("TX"));
    query.bindValue(QStringLiteral(":timestamp_utc"), event.timestampUtc.toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":endpoint"), notNullString(event.endpoint));
    query.bindValue(QStringLiteral(":payload"), event.payload);

    const bool ok = query.exec();
    if (!ok) {
        m_lastErrorText = QStringLiteral("写入原始通信事件失败：%1").arg(query.lastError().text());
    }
    return ok;
}

qint64 SessionStore::rawEventCount() const {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM raw_io_events"))) {
        return -1;
    }
    if (!query.next()) {
        return -1;
    }
    return query.value(0).toLongLong();
}

bool SessionStore::saveSendHistory(QString content, protocol::PayloadMode mode, protocol::LineEnding lineEnding, int limit) {
    if (content.isEmpty()) {
        return true;
    }

    const int safeLimit = qMax(1, limit);
    const int modeValue = static_cast<int>(mode);
    const int lineEndingValue = static_cast<int>(lineEnding);

    QSqlQuery deleteQuery(m_db);
    deleteQuery.prepare(QStringLiteral(R"sql(
        DELETE FROM send_history
        WHERE content = :content AND payload_mode = :payload_mode AND line_ending = :line_ending
    )sql"));
    deleteQuery.bindValue(QStringLiteral(":content"), content);
    deleteQuery.bindValue(QStringLiteral(":payload_mode"), modeValue);
    deleteQuery.bindValue(QStringLiteral(":line_ending"), lineEndingValue);
    if (!deleteQuery.exec()) {
        m_lastErrorText = QStringLiteral("更新发送历史失败：%1").arg(deleteQuery.lastError().text());
        return false;
    }

    QSqlQuery insertQuery(m_db);
    insertQuery.prepare(QStringLiteral(R"sql(
        INSERT INTO send_history(content, payload_mode, line_ending, sent_at_utc)
        VALUES(:content, :payload_mode, :line_ending, :sent_at_utc)
    )sql"));
    insertQuery.bindValue(QStringLiteral(":content"), content);
    insertQuery.bindValue(QStringLiteral(":payload_mode"), modeValue);
    insertQuery.bindValue(QStringLiteral(":line_ending"), lineEndingValue);
    insertQuery.bindValue(QStringLiteral(":sent_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!insertQuery.exec()) {
        m_lastErrorText = QStringLiteral("写入发送历史失败：%1").arg(insertQuery.lastError().text());
        return false;
    }

    QSqlQuery trimQuery(m_db);
    const bool trimmed = trimQuery.exec(QStringLiteral(R"sql(
        DELETE FROM send_history
        WHERE id NOT IN (
            SELECT id FROM send_history ORDER BY sent_at_utc DESC, id DESC LIMIT %1
        )
    )sql").arg(safeLimit));
    if (!trimmed) {
        m_lastErrorText = QStringLiteral("裁剪发送历史失败：%1").arg(trimQuery.lastError().text());
    }
    return trimmed;
}

QList<SendHistoryEntry> SessionStore::recentSendHistory(int limit) const {
    QList<SendHistoryEntry> entries;
    const int safeLimit = qMax(1, limit);
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT id, content, payload_mode, line_ending, sent_at_utc
        FROM send_history
        ORDER BY sent_at_utc DESC, id DESC
        LIMIT %1
    )sql").arg(safeLimit))) {
        return entries;
    }

    while (query.next()) {
        SendHistoryEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.content = query.value(1).toString();
        entry.payloadMode = static_cast<protocol::PayloadMode>(query.value(2).toInt());
        entry.lineEnding = static_cast<protocol::LineEnding>(query.value(3).toInt());
        entry.sentAtUtc = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        entries.append(entry);
    }
    return entries;
}

bool SessionStore::saveSerialProfile(const SerialProfile& profile) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        INSERT INTO serial_profiles(
            name, port_name, baud_rate, data_bits, parity, stop_bits, flow_control, dtr, rts, updated_at_utc
        ) VALUES (
            :name, :port_name, :baud_rate, :data_bits, :parity, :stop_bits, :flow_control, :dtr, :rts, :updated_at_utc
        )
        ON CONFLICT(name) DO UPDATE SET
            port_name = excluded.port_name,
            baud_rate = excluded.baud_rate,
            data_bits = excluded.data_bits,
            parity = excluded.parity,
            stop_bits = excluded.stop_bits,
            flow_control = excluded.flow_control,
            dtr = excluded.dtr,
            rts = excluded.rts,
            updated_at_utc = excluded.updated_at_utc
    )sql"));

    query.bindValue(QStringLiteral(":name"), profile.name.isEmpty() ? QStringLiteral("default") : profile.name);
    query.bindValue(QStringLiteral(":port_name"), profile.options.portName);
    query.bindValue(QStringLiteral(":baud_rate"), profile.options.baudRate);
    query.bindValue(QStringLiteral(":data_bits"), static_cast<int>(profile.options.dataBits));
    query.bindValue(QStringLiteral(":parity"), static_cast<int>(profile.options.parity));
    query.bindValue(QStringLiteral(":stop_bits"), static_cast<int>(profile.options.stopBits));
    query.bindValue(QStringLiteral(":flow_control"), static_cast<int>(profile.options.flowControl));
    query.bindValue(QStringLiteral(":dtr"), profile.options.dataTerminalReady ? 1 : 0);
    query.bindValue(QStringLiteral(":rts"), profile.options.requestToSend ? 1 : 0);
    query.bindValue(QStringLiteral(":updated_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    const bool ok = query.exec();
    if (!ok) {
        m_lastErrorText = QStringLiteral("保存串口配置失败：%1").arg(query.lastError().text());
    }
    return ok;
}

std::optional<SerialProfile> SessionStore::latestSerialProfile() const {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT name, port_name, baud_rate, data_bits, parity, stop_bits, flow_control, dtr, rts, updated_at_utc
        FROM serial_profiles
        ORDER BY updated_at_utc DESC, id DESC
        LIMIT 1
    )sql"))) {
        return std::nullopt;
    }

    if (!query.next()) {
        return std::nullopt;
    }

    SerialProfile profile;
    profile.name = query.value(0).toString();
    profile.options.sessionId = profile.name;
    profile.options.portName = query.value(1).toString();
    profile.options.baudRate = query.value(2).toInt();
    profile.options.dataBits = static_cast<QSerialPort::DataBits>(query.value(3).toInt());
    profile.options.parity = static_cast<QSerialPort::Parity>(query.value(4).toInt());
    profile.options.stopBits = static_cast<QSerialPort::StopBits>(query.value(5).toInt());
    profile.options.flowControl = static_cast<QSerialPort::FlowControl>(query.value(6).toInt());
    profile.options.dataTerminalReady = query.value(7).toInt() != 0;
    profile.options.requestToSend = query.value(8).toInt() != 0;
    profile.updatedAtUtc = QDateTime::fromString(query.value(9).toString(), Qt::ISODateWithMs);
    return profile;
}

void SessionStore::closeDatabaseConnection() {
    const QString connectionName = m_connectionName;
    if (m_db.isValid()) {
        if (m_db.isOpen()) {
            m_db.close();
        }
        m_db = QSqlDatabase();
    }
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase::removeDatabase(connectionName);
    }
}

} // namespace svm::storage
