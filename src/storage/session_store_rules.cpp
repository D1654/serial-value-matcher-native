#include "storage/session_store.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>


namespace svm::storage {

bool SessionStore::saveProtocolFieldRule(const ProtocolFieldRuleRecord& rule) {
    if (rule.ruleId.isEmpty()) {
        m_lastErrorText = QStringLiteral("保存协议字段规则失败：规则 ID 不能为空。");
        return false;
    }
    if (rule.fieldName.trimmed().isEmpty()) {
        m_lastErrorText = QStringLiteral("保存协议字段规则失败：字段名称不能为空。");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        INSERT INTO protocol_field_rules(
            rule_id, field_name, source_stability_run_id, source_stable_candidate_id,
            candidate_type, word_order, byte_order, slave_id, function_code, start_address,
            register_count, scale_multiplier, scale_offset, unit, confidence_level, stability_score,
            evidence_summary, interpretation_map, created_at_utc
        ) VALUES (
            :rule_id, :field_name, :source_stability_run_id, :source_stable_candidate_id,
            :candidate_type, :word_order, :byte_order, :slave_id, :function_code, :start_address,
            :register_count, :scale_multiplier, :scale_offset, :unit, :confidence_level, :stability_score,
            :evidence_summary, :interpretation_map, :created_at_utc
        )
        ON CONFLICT(rule_id) DO UPDATE SET
            field_name = excluded.field_name,
            source_stability_run_id = excluded.source_stability_run_id,
            source_stable_candidate_id = excluded.source_stable_candidate_id,
            candidate_type = excluded.candidate_type,
            word_order = excluded.word_order,
            byte_order = excluded.byte_order,
            slave_id = excluded.slave_id,
            function_code = excluded.function_code,
            start_address = excluded.start_address,
            register_count = excluded.register_count,
            scale_multiplier = excluded.scale_multiplier,
            scale_offset = excluded.scale_offset,
            unit = excluded.unit,
            confidence_level = excluded.confidence_level,
            stability_score = excluded.stability_score,
            evidence_summary = excluded.evidence_summary,
            interpretation_map = excluded.interpretation_map,
            created_at_utc = excluded.created_at_utc
    )sql"));
    query.bindValue(QStringLiteral(":rule_id"), rule.ruleId);
    query.bindValue(QStringLiteral(":field_name"), rule.fieldName.trimmed());
    query.bindValue(QStringLiteral(":source_stability_run_id"), notNullString(rule.sourceStabilityRunId));
    query.bindValue(QStringLiteral(":source_stable_candidate_id"), rule.sourceStableCandidateId);
    query.bindValue(QStringLiteral(":candidate_type"), notNullString(rule.candidateType));
    query.bindValue(QStringLiteral(":word_order"), notNullString(rule.wordOrder));
    query.bindValue(QStringLiteral(":byte_order"), notNullString(rule.byteOrder));
    query.bindValue(QStringLiteral(":slave_id"), rule.slaveId);
    query.bindValue(QStringLiteral(":function_code"), rule.functionCode);
    query.bindValue(QStringLiteral(":start_address"), rule.startAddress);
    query.bindValue(QStringLiteral(":register_count"), rule.registerCount);
    query.bindValue(QStringLiteral(":scale_multiplier"), rule.scaleMultiplier);
    query.bindValue(QStringLiteral(":scale_offset"), rule.scaleOffset);
    query.bindValue(QStringLiteral(":unit"), notNullString(rule.unit));
    query.bindValue(QStringLiteral(":confidence_level"), notNullString(rule.confidenceLevel));
    query.bindValue(QStringLiteral(":stability_score"), rule.stabilityScore);
    query.bindValue(QStringLiteral(":evidence_summary"), notNullString(rule.evidenceSummary));
    query.bindValue(QStringLiteral(":interpretation_map"), notNullString(rule.interpretationMap));
    query.bindValue(QStringLiteral(":created_at_utc"), dateToString(rule.createdAtUtc.isValid() ? rule.createdAtUtc : QDateTime::currentDateTimeUtc()));

    if (!query.exec()) {
        m_lastErrorText = QStringLiteral("保存协议字段规则失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

bool SessionStore::deleteProtocolFieldRule(const QString& ruleId) {
    if (ruleId.trimmed().isEmpty()) {
        m_lastErrorText = QStringLiteral("删除协议字段规则失败：规则 ID 不能为空。");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM protocol_field_rules WHERE rule_id = :rule_id"));
    query.bindValue(QStringLiteral(":rule_id"), ruleId.trimmed());
    if (!query.exec()) {
        m_lastErrorText = QStringLiteral("删除协议字段规则失败：%1").arg(query.lastError().text());
        return false;
    }
    return true;
}

std::optional<ProtocolFieldRuleRecord> SessionStore::protocolFieldRule(const QString& ruleId) const {
    clearReadError();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        SELECT id, rule_id, field_name, source_stability_run_id, source_stable_candidate_id,
               candidate_type, word_order, byte_order, slave_id, function_code, start_address,
               register_count, scale_multiplier, scale_offset, unit, confidence_level, stability_score,
               evidence_summary, interpretation_map, created_at_utc
        FROM protocol_field_rules
        WHERE rule_id = :rule_id
    )sql"));
    query.bindValue(QStringLiteral(":rule_id"), ruleId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取协议字段规则失败"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    ProtocolFieldRuleRecord record;
    record.id = query.value(0).toLongLong();
    record.ruleId = query.value(1).toString();
    record.fieldName = query.value(2).toString();
    record.sourceStabilityRunId = query.value(3).toString();
    record.sourceStableCandidateId = query.value(4).toLongLong();
    record.candidateType = query.value(5).toString();
    record.wordOrder = query.value(6).toString();
    record.byteOrder = query.value(7).toString();
    record.slaveId = query.value(8).toInt();
    record.functionCode = query.value(9).toInt();
    record.startAddress = query.value(10).toInt();
    record.registerCount = query.value(11).toInt();
    record.scaleMultiplier = query.value(12).toDouble();
    record.scaleOffset = query.value(13).toDouble();
    record.unit = query.value(14).toString();
    record.confidenceLevel = query.value(15).toString();
    record.stabilityScore = query.value(16).toDouble();
    record.evidenceSummary = query.value(17).toString();
    record.interpretationMap = query.value(18).toString();
    record.createdAtUtc = dateFromString(query.value(19).toString());
    return record;
}

QList<ProtocolFieldRuleRecord> SessionStore::recentProtocolFieldRules(int limit) const {
    clearReadError();
    QList<ProtocolFieldRuleRecord> rules;
    const int safeLimit = qMax(1, limit);
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT id, rule_id, field_name, source_stability_run_id, source_stable_candidate_id,
               candidate_type, word_order, byte_order, slave_id, function_code, start_address,
               register_count, scale_multiplier, scale_offset, unit, confidence_level, stability_score,
               evidence_summary, interpretation_map, created_at_utc
        FROM protocol_field_rules
        ORDER BY created_at_utc DESC, id DESC
        LIMIT %1
    )sql").arg(safeLimit))) {
        setReadError(QStringLiteral("读取最近协议字段规则失败"), query.lastError());
        return rules;
    }

    while (query.next()) {
        ProtocolFieldRuleRecord record;
        record.id = query.value(0).toLongLong();
        record.ruleId = query.value(1).toString();
        record.fieldName = query.value(2).toString();
        record.sourceStabilityRunId = query.value(3).toString();
        record.sourceStableCandidateId = query.value(4).toLongLong();
        record.candidateType = query.value(5).toString();
        record.wordOrder = query.value(6).toString();
        record.byteOrder = query.value(7).toString();
        record.slaveId = query.value(8).toInt();
        record.functionCode = query.value(9).toInt();
        record.startAddress = query.value(10).toInt();
        record.registerCount = query.value(11).toInt();
        record.scaleMultiplier = query.value(12).toDouble();
        record.scaleOffset = query.value(13).toDouble();
        record.unit = query.value(14).toString();
        record.confidenceLevel = query.value(15).toString();
        record.stabilityScore = query.value(16).toDouble();
        record.evidenceSummary = query.value(17).toString();
        record.interpretationMap = query.value(18).toString();
        record.createdAtUtc = dateFromString(query.value(19).toString());
        rules.append(record);
    }
    return rules;
}

bool SessionStore::saveRuleVerificationRun(const RuleVerificationRunRecord& run, const matching::ProtocolRuleVerificationSummary& summary) {
    if (run.verificationRunId.trimmed().isEmpty()) {
        m_lastErrorText = QStringLiteral("保存规则验证结果失败：验证运行 ID 不能为空。");
        return false;
    }
    if (run.sourceScanSessionId.trimmed().isEmpty()) {
        m_lastErrorText = QStringLiteral("保存规则验证结果失败：来源扫描会话不能为空。");
        return false;
    }

    if (!m_db.transaction()) {
        m_lastErrorText = QStringLiteral("保存规则验证结果失败：无法开启数据库事务：%1").arg(m_db.lastError().text());
        return false;
    }

    auto rollback = [this](const QString& message) {
        m_lastErrorText = message;
        m_db.rollback();
        return false;
    };

    QSqlQuery deleteResults(m_db);
    deleteResults.prepare(QStringLiteral("DELETE FROM rule_verification_results WHERE verification_run_id = :verification_run_id"));
    deleteResults.bindValue(QStringLiteral(":verification_run_id"), run.verificationRunId);
    if (!deleteResults.exec()) {
        return rollback(QStringLiteral("保存规则验证结果失败：清理旧验证明细失败：%1").arg(deleteResults.lastError().text()));
    }

    QSqlQuery runQuery(m_db);
    runQuery.prepare(QStringLiteral(R"sql(
        INSERT INTO rule_verification_runs(
            verification_run_id, source_scan_session_id, rule_count, verified_count,
            missing_count, unsupported_count, created_at_utc
        ) VALUES (
            :verification_run_id, :source_scan_session_id, :rule_count, :verified_count,
            :missing_count, :unsupported_count, :created_at_utc
        )
        ON CONFLICT(verification_run_id) DO UPDATE SET
            source_scan_session_id = excluded.source_scan_session_id,
            rule_count = excluded.rule_count,
            verified_count = excluded.verified_count,
            missing_count = excluded.missing_count,
            unsupported_count = excluded.unsupported_count,
            created_at_utc = excluded.created_at_utc
    )sql"));
    runQuery.bindValue(QStringLiteral(":verification_run_id"), run.verificationRunId);
    runQuery.bindValue(QStringLiteral(":source_scan_session_id"), run.sourceScanSessionId.trimmed());
    runQuery.bindValue(QStringLiteral(":rule_count"), summary.totalRules);
    runQuery.bindValue(QStringLiteral(":verified_count"), summary.verifiedRules);
    runQuery.bindValue(QStringLiteral(":missing_count"), summary.missingRules);
    runQuery.bindValue(QStringLiteral(":unsupported_count"), summary.unsupportedRules);
    runQuery.bindValue(QStringLiteral(":created_at_utc"), dateToString(run.createdAtUtc.isValid() ? run.createdAtUtc : QDateTime::currentDateTimeUtc()));
    if (!runQuery.exec()) {
        return rollback(QStringLiteral("保存规则验证运行失败：%1").arg(runQuery.lastError().text()));
    }

    for (const matching::ProtocolRuleVerificationResult& result : summary.results) {
        QSqlQuery resultQuery(m_db);
        resultQuery.prepare(QStringLiteral(R"sql(
            INSERT INTO rule_verification_results(
                verification_run_id, rule_id, field_name, unit, candidate_type, source_scan_session_id, verified,
                status_text, slave_id, function_code, start_address, register_count, observation_ids,
                raw_registers, decoded_value, engineering_value, observed_at_utc, interpretation_text, evidence_text
            ) VALUES (
                :verification_run_id, :rule_id, :field_name, :unit, :candidate_type, :source_scan_session_id, :verified,
                :status_text, :slave_id, :function_code, :start_address, :register_count, :observation_ids,
                :raw_registers, :decoded_value, :engineering_value, :observed_at_utc, :interpretation_text, :evidence_text
            )
        )sql"));
        resultQuery.bindValue(QStringLiteral(":verification_run_id"), run.verificationRunId);
        resultQuery.bindValue(QStringLiteral(":rule_id"), notNullString(result.ruleId));
        resultQuery.bindValue(QStringLiteral(":field_name"), notNullString(result.fieldName));
        resultQuery.bindValue(QStringLiteral(":unit"), notNullString(result.unit));
        resultQuery.bindValue(QStringLiteral(":candidate_type"), notNullString(result.candidateType));
        resultQuery.bindValue(QStringLiteral(":source_scan_session_id"), result.sourceScanSessionId.isEmpty() ? run.sourceScanSessionId.trimmed() : result.sourceScanSessionId);
        resultQuery.bindValue(QStringLiteral(":verified"), result.verified ? 1 : 0);
        resultQuery.bindValue(QStringLiteral(":status_text"), notNullString(result.statusText));
        resultQuery.bindValue(QStringLiteral(":slave_id"), result.slaveId);
        resultQuery.bindValue(QStringLiteral(":function_code"), result.functionCode);
        resultQuery.bindValue(QStringLiteral(":start_address"), result.startAddress);
        resultQuery.bindValue(QStringLiteral(":register_count"), result.registerCount);
        resultQuery.bindValue(QStringLiteral(":observation_ids"), notNullString(joinLongList(result.observationIds)));
        resultQuery.bindValue(QStringLiteral(":raw_registers"), notNullString(joinUInt16List(result.rawRegisters)));
        resultQuery.bindValue(QStringLiteral(":decoded_value"), result.decodedValue);
        resultQuery.bindValue(QStringLiteral(":engineering_value"), result.engineeringValue);
        resultQuery.bindValue(QStringLiteral(":observed_at_utc"), dateToString(result.observedAtUtc));
        resultQuery.bindValue(QStringLiteral(":interpretation_text"), notNullString(result.interpretationText));
        resultQuery.bindValue(QStringLiteral(":evidence_text"), notNullString(result.evidenceText));
        if (!resultQuery.exec()) {
            return rollback(QStringLiteral("保存规则验证明细失败：%1").arg(resultQuery.lastError().text()));
        }
    }

    if (!m_db.commit()) {
        return rollback(QStringLiteral("保存规则验证结果失败：提交事务失败：%1").arg(m_db.lastError().text()));
    }
    return true;
}

std::optional<RuleVerificationRunRecord> SessionStore::ruleVerificationRun(const QString& verificationRunId) const {
    clearReadError();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        SELECT id, verification_run_id, source_scan_session_id, rule_count, verified_count,
               missing_count, unsupported_count, created_at_utc
        FROM rule_verification_runs
        WHERE verification_run_id = :verification_run_id
    )sql"));
    query.bindValue(QStringLiteral(":verification_run_id"), verificationRunId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取规则验证运行失败"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    RuleVerificationRunRecord record;
    record.id = query.value(0).toLongLong();
    record.verificationRunId = query.value(1).toString();
    record.sourceScanSessionId = query.value(2).toString();
    record.ruleCount = query.value(3).toInt();
    record.verifiedCount = query.value(4).toInt();
    record.missingCount = query.value(5).toInt();
    record.unsupportedCount = query.value(6).toInt();
    record.createdAtUtc = dateFromString(query.value(7).toString());
    return record;
}

std::optional<RuleVerificationRunRecord> SessionStore::latestRuleVerificationRun() const {
    clearReadError();
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT id, verification_run_id, source_scan_session_id, rule_count, verified_count,
               missing_count, unsupported_count, created_at_utc
        FROM rule_verification_runs
        ORDER BY created_at_utc DESC, id DESC
        LIMIT 1
    )sql"))) {
        setReadError(QStringLiteral("读取最近规则验证运行失败"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    RuleVerificationRunRecord record;
    record.id = query.value(0).toLongLong();
    record.verificationRunId = query.value(1).toString();
    record.sourceScanSessionId = query.value(2).toString();
    record.ruleCount = query.value(3).toInt();
    record.verifiedCount = query.value(4).toInt();
    record.missingCount = query.value(5).toInt();
    record.unsupportedCount = query.value(6).toInt();
    record.createdAtUtc = dateFromString(query.value(7).toString());
    return record;
}

QList<RuleVerificationResultRecord> SessionStore::ruleVerificationResults(const QString& verificationRunId) const {
    clearReadError();
    QList<RuleVerificationResultRecord> results;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(R"sql(
        SELECT id, verification_run_id, rule_id, field_name, unit, candidate_type, source_scan_session_id, verified,
               status_text, slave_id, function_code, start_address, register_count, observation_ids,
               raw_registers, decoded_value, engineering_value, observed_at_utc, interpretation_text, evidence_text
        FROM rule_verification_results
        WHERE verification_run_id = :verification_run_id
        ORDER BY id ASC
    )sql"));
    query.bindValue(QStringLiteral(":verification_run_id"), verificationRunId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取规则验证明细失败"), query.lastError());
        return results;
    }

    while (query.next()) {
        RuleVerificationResultRecord record;
        record.id = query.value(0).toLongLong();
        record.verificationRunId = query.value(1).toString();
        record.ruleId = query.value(2).toString();
        record.fieldName = query.value(3).toString();
        record.unit = query.value(4).toString();
        record.candidateType = query.value(5).toString();
        record.sourceScanSessionId = query.value(6).toString();
        record.verified = query.value(7).toInt() != 0;
        record.statusText = query.value(8).toString();
        record.slaveId = query.value(9).toInt();
        record.functionCode = query.value(10).toInt();
        record.startAddress = query.value(11).toInt();
        record.registerCount = query.value(12).toInt();
        record.observationIds = parseLongList(query.value(13).toString());
        record.rawRegisters = parseIntList(query.value(14).toString());
        record.decodedValue = query.value(15).toDouble();
        record.engineeringValue = query.value(16).toDouble();
        record.observedAtUtc = dateFromString(query.value(17).toString());
        record.interpretationText = query.value(18).toString();
        record.evidenceText = query.value(19).toString();
        results.append(record);
    }
    return results;
}

} // namespace svm::storage
