#include "storage/session_store.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>


namespace svm::storage {

bool SessionStore::saveMatchRun(const MatchRunRecord& run, const QList<matching::ValueMatchCandidate>& candidates) {
    if (run.runId.isEmpty()) {
        m_lastErrorText = QStringLiteral("保存匹配候选失败：匹配运行 ID 不能为空。");
        return false;
    }

    if (!m_db.transaction()) {
        m_lastErrorText = QStringLiteral("保存匹配候选失败：无法开启数据库事务：%1").arg(m_db.lastError().text());
        return false;
    }

    auto rollback = [this](const QString& message) {
        m_lastErrorText = message;
        m_db.rollback();
        return false;
    };

    QSqlQuery deleteCandidates(m_db);
    deleteCandidates.prepare(QStringLiteral("DELETE FROM match_candidates WHERE run_id = :run_id"));
    deleteCandidates.bindValue(QStringLiteral(":run_id"), run.runId);
    if (!deleteCandidates.exec()) {
        return rollback(QStringLiteral("保存匹配候选失败：清理旧候选失败：%1").arg(deleteCandidates.lastError().text()));
    }

    QSqlQuery runQuery(m_db);
    runQuery.prepare(QStringLiteral(R"sql(
        INSERT INTO match_runs(
            run_id, source_scan_session_id, target_label, target_value, target_unit, sampled_at_utc,
            tolerance_absolute, tolerance_relative_ratio, candidate_count, created_at_utc
        ) VALUES (
            :run_id, :source_scan_session_id, :target_label, :target_value, :target_unit, :sampled_at_utc,
            :tolerance_absolute, :tolerance_relative_ratio, :candidate_count, :created_at_utc
        )
        ON CONFLICT(run_id) DO UPDATE SET
            source_scan_session_id = excluded.source_scan_session_id,
            target_label = excluded.target_label,
            target_value = excluded.target_value,
            target_unit = excluded.target_unit,
            sampled_at_utc = excluded.sampled_at_utc,
            tolerance_absolute = excluded.tolerance_absolute,
            tolerance_relative_ratio = excluded.tolerance_relative_ratio,
            candidate_count = excluded.candidate_count,
            created_at_utc = excluded.created_at_utc
    )sql"));
    runQuery.bindValue(QStringLiteral(":run_id"), run.runId);
    runQuery.bindValue(QStringLiteral(":source_scan_session_id"), notNullString(run.sourceScanSessionId));
    runQuery.bindValue(QStringLiteral(":target_label"), notNullString(run.targetLabel));
    runQuery.bindValue(QStringLiteral(":target_value"), run.targetValue);
    runQuery.bindValue(QStringLiteral(":target_unit"), notNullString(run.targetUnit));
    runQuery.bindValue(QStringLiteral(":sampled_at_utc"), dateToString(run.sampledAtUtc));
    runQuery.bindValue(QStringLiteral(":tolerance_absolute"), run.toleranceAbsolute);
    runQuery.bindValue(QStringLiteral(":tolerance_relative_ratio"), run.toleranceRelativeRatio);
    runQuery.bindValue(QStringLiteral(":candidate_count"), candidates.size());
    runQuery.bindValue(QStringLiteral(":created_at_utc"), dateToString(run.createdAtUtc.isValid() ? run.createdAtUtc : QDateTime::currentDateTimeUtc()));
    if (!runQuery.exec()) {
        return rollback(QStringLiteral("保存匹配运行失败：%1").arg(runQuery.lastError().text()));
    }

    int rankIndex = 0;
    for (const matching::ValueMatchCandidate& candidate : candidates) {
        QSqlQuery candidateQuery(m_db);
        candidateQuery.prepare(QStringLiteral(R"sql(
            INSERT INTO match_candidates(
                run_id, rank_index, candidate_type, word_order, byte_order, source_session_id,
                slave_id, function_code, start_address, register_count, observation_ids, addresses,
                block_indexes, attempt_indexes, raw_registers, decoded_value, scale_multiplier,
                scale_offset, engineering_value, delta, absolute_error, effective_tolerance, score,
                observed_at_utc, evidence_text
            ) VALUES (
                :run_id, :rank_index, :candidate_type, :word_order, :byte_order, :source_session_id,
                :slave_id, :function_code, :start_address, :register_count, :observation_ids, :addresses,
                :block_indexes, :attempt_indexes, :raw_registers, :decoded_value, :scale_multiplier,
                :scale_offset, :engineering_value, :delta, :absolute_error, :effective_tolerance, :score,
                :observed_at_utc, :evidence_text
            )
        )sql"));
        candidateQuery.bindValue(QStringLiteral(":run_id"), run.runId);
        candidateQuery.bindValue(QStringLiteral(":rank_index"), rankIndex);
        candidateQuery.bindValue(QStringLiteral(":candidate_type"), candidateTypeKey(candidate.type));
        candidateQuery.bindValue(QStringLiteral(":word_order"), wordOrderKey(candidate.wordOrder));
        candidateQuery.bindValue(QStringLiteral(":byte_order"), byteOrderKey(candidate.byteOrder));
        candidateQuery.bindValue(QStringLiteral(":source_session_id"), notNullString(candidate.sessionId));
        candidateQuery.bindValue(QStringLiteral(":slave_id"), candidate.slaveId);
        candidateQuery.bindValue(QStringLiteral(":function_code"), candidate.functionCode);
        candidateQuery.bindValue(QStringLiteral(":start_address"), candidate.startAddress);
        candidateQuery.bindValue(QStringLiteral(":register_count"), candidate.registerCount);
        candidateQuery.bindValue(QStringLiteral(":observation_ids"), joinLongList(candidate.observationIds));
        candidateQuery.bindValue(QStringLiteral(":addresses"), joinIntList(candidate.addresses));
        candidateQuery.bindValue(QStringLiteral(":block_indexes"), joinIntList(candidate.blockIndexes));
        candidateQuery.bindValue(QStringLiteral(":attempt_indexes"), joinIntList(candidate.attemptIndexes));
        candidateQuery.bindValue(QStringLiteral(":raw_registers"), joinUInt16List(candidate.rawRegisters));
        candidateQuery.bindValue(QStringLiteral(":decoded_value"), candidate.decodedValue);
        candidateQuery.bindValue(QStringLiteral(":scale_multiplier"), candidate.scale.multiplier);
        candidateQuery.bindValue(QStringLiteral(":scale_offset"), candidate.scale.offset);
        candidateQuery.bindValue(QStringLiteral(":engineering_value"), candidate.engineeringValue);
        candidateQuery.bindValue(QStringLiteral(":delta"), candidate.delta);
        candidateQuery.bindValue(QStringLiteral(":absolute_error"), candidate.absoluteError);
        candidateQuery.bindValue(QStringLiteral(":effective_tolerance"), candidate.effectiveTolerance);
        candidateQuery.bindValue(QStringLiteral(":score"), candidate.score);
        candidateQuery.bindValue(QStringLiteral(":observed_at_utc"), dateToString(candidate.observedAtUtc));
        candidateQuery.bindValue(QStringLiteral(":evidence_text"), notNullString(candidate.evidenceText));
        if (!candidateQuery.exec()) {
            return rollback(QStringLiteral("保存匹配候选失败：%1").arg(candidateQuery.lastError().text()));
        }
        ++rankIndex;
    }

    if (!m_db.commit()) {
        return rollback(QStringLiteral("保存匹配候选失败：提交事务失败：%1").arg(m_db.lastError().text()));
    }

    return true;
}

std::optional<MatchRunRecord> SessionStore::matchRun(const QString& runId) const {
    clearReadError();
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT run_id, source_scan_session_id, target_label, target_value, target_unit, sampled_at_utc,
               tolerance_absolute, tolerance_relative_ratio, candidate_count, created_at_utc
        FROM match_runs
        WHERE run_id = :run_id
    )sql"))) {
        setReadError(QStringLiteral("读取匹配运行失败"), query.lastError());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":run_id"), runId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取匹配运行失败"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    MatchRunRecord record;
    record.runId = query.value(0).toString();
    record.sourceScanSessionId = query.value(1).toString();
    record.targetLabel = query.value(2).toString();
    record.targetValue = query.value(3).toDouble();
    record.targetUnit = query.value(4).toString();
    record.sampledAtUtc = dateFromString(query.value(5).toString());
    record.toleranceAbsolute = query.value(6).toDouble();
    record.toleranceRelativeRatio = query.value(7).toDouble();
    record.candidateCount = query.value(8).toInt();
    record.createdAtUtc = dateFromString(query.value(9).toString());
    return record;
}

QList<MatchRunRecord> SessionStore::recentMatchRuns(int limit) const {
    clearReadError();
    QList<MatchRunRecord> runs;
    const int safeLimit = limit <= 0 ? 1 : limit;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT run_id, source_scan_session_id, target_label, target_value, target_unit, sampled_at_utc,
               tolerance_absolute, tolerance_relative_ratio, candidate_count, created_at_utc
        FROM match_runs
        ORDER BY created_at_utc DESC, run_id DESC
        LIMIT %1
    )sql").arg(safeLimit))) {
        setReadError(QStringLiteral("读取最近匹配运行失败"), query.lastError());
        return runs;
    }

    while (query.next()) {
        MatchRunRecord record;
        record.runId = query.value(0).toString();
        record.sourceScanSessionId = query.value(1).toString();
        record.targetLabel = query.value(2).toString();
        record.targetValue = query.value(3).toDouble();
        record.targetUnit = query.value(4).toString();
        record.sampledAtUtc = dateFromString(query.value(5).toString());
        record.toleranceAbsolute = query.value(6).toDouble();
        record.toleranceRelativeRatio = query.value(7).toDouble();
        record.candidateCount = query.value(8).toInt();
        record.createdAtUtc = dateFromString(query.value(9).toString());
        runs.append(record);
    }
    return runs;
}

QList<MatchCandidateRecord> SessionStore::matchCandidates(const QString& runId) const {
    clearReadError();
    QList<MatchCandidateRecord> candidates;
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT id, run_id, rank_index, candidate_type, word_order, byte_order, source_session_id,
               slave_id, function_code, start_address, register_count, observation_ids, addresses,
               block_indexes, attempt_indexes, raw_registers, decoded_value, scale_multiplier,
               scale_offset, engineering_value, delta, absolute_error, effective_tolerance, score,
               observed_at_utc, evidence_text
        FROM match_candidates
        WHERE run_id = :run_id
        ORDER BY rank_index ASC, id ASC
    )sql"))) {
        setReadError(QStringLiteral("读取匹配候选失败"), query.lastError());
        return candidates;
    }
    query.bindValue(QStringLiteral(":run_id"), runId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取匹配候选失败"), query.lastError());
        return candidates;
    }

    while (query.next()) {
        MatchCandidateRecord record;
        record.id = query.value(0).toLongLong();
        record.runId = query.value(1).toString();
        record.rankIndex = query.value(2).toInt();
        record.candidateType = query.value(3).toString();
        record.wordOrder = query.value(4).toString();
        record.byteOrder = query.value(5).toString();
        record.sourceSessionId = query.value(6).toString();
        record.slaveId = query.value(7).toInt();
        record.functionCode = query.value(8).toInt();
        record.startAddress = query.value(9).toInt();
        record.registerCount = query.value(10).toInt();
        record.observationIds = parseLongList(query.value(11).toString());
        record.addresses = parseIntList(query.value(12).toString());
        record.blockIndexes = parseIntList(query.value(13).toString());
        record.attemptIndexes = parseIntList(query.value(14).toString());
        record.rawRegisters = parseIntList(query.value(15).toString());
        record.decodedValue = query.value(16).toDouble();
        record.scaleMultiplier = query.value(17).toDouble();
        record.scaleOffset = query.value(18).toDouble();
        record.engineeringValue = query.value(19).toDouble();
        record.delta = query.value(20).toDouble();
        record.absoluteError = query.value(21).toDouble();
        record.effectiveTolerance = query.value(22).toDouble();
        record.score = query.value(23).toDouble();
        record.observedAtUtc = dateFromString(query.value(24).toString());
        record.evidenceText = query.value(25).toString();
        candidates.append(record);
    }
    return candidates;
}

bool SessionStore::saveStabilityRun(const StabilityRunRecord& run, const QList<matching::StableCandidate>& candidates) {
    if (run.stabilityRunId.isEmpty()) {
        m_lastErrorText = QStringLiteral("保存稳定性分析结果失败：稳定性运行 ID 不能为空。");
        return false;
    }

    if (!m_db.transaction()) {
        m_lastErrorText = QStringLiteral("保存稳定性分析结果失败：无法开启数据库事务：%1").arg(m_db.lastError().text());
        return false;
    }

    auto rollback = [this](const QString& message) {
        m_lastErrorText = message;
        m_db.rollback();
        return false;
    };

    QSqlQuery deleteCandidates(m_db);
    deleteCandidates.prepare(QStringLiteral("DELETE FROM stable_candidates WHERE stability_run_id = :stability_run_id"));
    deleteCandidates.bindValue(QStringLiteral(":stability_run_id"), run.stabilityRunId);
    if (!deleteCandidates.exec()) {
        return rollback(QStringLiteral("保存稳定性分析结果失败：清理旧稳定候选失败：%1").arg(deleteCandidates.lastError().text()));
    }

    QSqlQuery runQuery(m_db);
    runQuery.prepare(QStringLiteral(R"sql(
        INSERT INTO stability_runs(
            stability_run_id, source_match_run_ids, minimum_sample_count, strong_sample_count,
            stable_candidate_count, created_at_utc
        ) VALUES (
            :stability_run_id, :source_match_run_ids, :minimum_sample_count, :strong_sample_count,
            :stable_candidate_count, :created_at_utc
        )
        ON CONFLICT(stability_run_id) DO UPDATE SET
            source_match_run_ids = excluded.source_match_run_ids,
            minimum_sample_count = excluded.minimum_sample_count,
            strong_sample_count = excluded.strong_sample_count,
            stable_candidate_count = excluded.stable_candidate_count,
            created_at_utc = excluded.created_at_utc
    )sql"));
    runQuery.bindValue(QStringLiteral(":stability_run_id"), run.stabilityRunId);
    runQuery.bindValue(QStringLiteral(":source_match_run_ids"), joinStringList(run.sourceMatchRunIds));
    runQuery.bindValue(QStringLiteral(":minimum_sample_count"), run.minimumSampleCount);
    runQuery.bindValue(QStringLiteral(":strong_sample_count"), run.strongSampleCount);
    runQuery.bindValue(QStringLiteral(":stable_candidate_count"), candidates.size());
    runQuery.bindValue(QStringLiteral(":created_at_utc"), dateToString(run.createdAtUtc.isValid() ? run.createdAtUtc : QDateTime::currentDateTimeUtc()));
    if (!runQuery.exec()) {
        return rollback(QStringLiteral("保存稳定性运行失败：%1").arg(runQuery.lastError().text()));
    }

    int rankIndex = 0;
    for (const matching::StableCandidate& candidate : candidates) {
        QSqlQuery candidateQuery(m_db);
        candidateQuery.prepare(QStringLiteral(R"sql(
            INSERT INTO stable_candidates(
                stability_run_id, rank_index, candidate_type, word_order, byte_order,
                slave_id, function_code, start_address, register_count, scale_multiplier, scale_offset,
                sample_count, meets_minimum_sample_count, confidence_level, run_ids, source_scan_session_ids,
                observation_ids, addresses, mean_target_value, mean_engineering_value, mean_absolute_error,
                max_absolute_error, mean_candidate_score, mean_error_quality, sample_quality, stability_score,
                evidence_summary
            ) VALUES (
                :stability_run_id, :rank_index, :candidate_type, :word_order, :byte_order,
                :slave_id, :function_code, :start_address, :register_count, :scale_multiplier, :scale_offset,
                :sample_count, :meets_minimum_sample_count, :confidence_level, :run_ids, :source_scan_session_ids,
                :observation_ids, :addresses, :mean_target_value, :mean_engineering_value, :mean_absolute_error,
                :max_absolute_error, :mean_candidate_score, :mean_error_quality, :sample_quality, :stability_score,
                :evidence_summary
            )
        )sql"));
        candidateQuery.bindValue(QStringLiteral(":stability_run_id"), run.stabilityRunId);
        candidateQuery.bindValue(QStringLiteral(":rank_index"), rankIndex);
        candidateQuery.bindValue(QStringLiteral(":candidate_type"), candidate.candidateType);
        candidateQuery.bindValue(QStringLiteral(":word_order"), candidate.wordOrder);
        candidateQuery.bindValue(QStringLiteral(":byte_order"), candidate.byteOrder);
        candidateQuery.bindValue(QStringLiteral(":slave_id"), candidate.slaveId);
        candidateQuery.bindValue(QStringLiteral(":function_code"), candidate.functionCode);
        candidateQuery.bindValue(QStringLiteral(":start_address"), candidate.startAddress);
        candidateQuery.bindValue(QStringLiteral(":register_count"), candidate.registerCount);
        candidateQuery.bindValue(QStringLiteral(":scale_multiplier"), candidate.scaleMultiplier);
        candidateQuery.bindValue(QStringLiteral(":scale_offset"), candidate.scaleOffset);
        candidateQuery.bindValue(QStringLiteral(":sample_count"), candidate.sampleCount);
        candidateQuery.bindValue(QStringLiteral(":meets_minimum_sample_count"), candidate.meetsMinimumSampleCount ? 1 : 0);
        candidateQuery.bindValue(QStringLiteral(":confidence_level"), candidate.confidenceLevel);
        candidateQuery.bindValue(QStringLiteral(":run_ids"), joinStringList(candidate.runIds));
        candidateQuery.bindValue(QStringLiteral(":source_scan_session_ids"), joinStringList(candidate.sourceScanSessionIds));
        candidateQuery.bindValue(QStringLiteral(":observation_ids"), joinLongList(candidate.observationIds));
        candidateQuery.bindValue(QStringLiteral(":addresses"), joinIntList(candidate.addresses));
        candidateQuery.bindValue(QStringLiteral(":mean_target_value"), candidate.meanTargetValue);
        candidateQuery.bindValue(QStringLiteral(":mean_engineering_value"), candidate.meanEngineeringValue);
        candidateQuery.bindValue(QStringLiteral(":mean_absolute_error"), candidate.meanAbsoluteError);
        candidateQuery.bindValue(QStringLiteral(":max_absolute_error"), candidate.maxAbsoluteError);
        candidateQuery.bindValue(QStringLiteral(":mean_candidate_score"), candidate.meanCandidateScore);
        candidateQuery.bindValue(QStringLiteral(":mean_error_quality"), candidate.meanErrorQuality);
        candidateQuery.bindValue(QStringLiteral(":sample_quality"), candidate.sampleQuality);
        candidateQuery.bindValue(QStringLiteral(":stability_score"), candidate.stabilityScore);
        candidateQuery.bindValue(QStringLiteral(":evidence_summary"), notNullString(candidate.evidenceSummary));
        if (!candidateQuery.exec()) {
            return rollback(QStringLiteral("保存稳定候选失败：%1").arg(candidateQuery.lastError().text()));
        }
        ++rankIndex;
    }

    if (!m_db.commit()) {
        return rollback(QStringLiteral("保存稳定性分析结果失败：提交事务失败：%1").arg(m_db.lastError().text()));
    }

    return true;
}

std::optional<StabilityRunRecord> SessionStore::stabilityRun(const QString& stabilityRunId) const {
    clearReadError();
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT stability_run_id, source_match_run_ids, minimum_sample_count, strong_sample_count,
               stable_candidate_count, created_at_utc
        FROM stability_runs
        WHERE stability_run_id = :stability_run_id
    )sql"))) {
        setReadError(QStringLiteral("读取稳定性运行失败"), query.lastError());
        return std::nullopt;
    }
    query.bindValue(QStringLiteral(":stability_run_id"), stabilityRunId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取稳定性运行失败"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    StabilityRunRecord record;
    record.stabilityRunId = query.value(0).toString();
    record.sourceMatchRunIds = parseStringList(query.value(1).toString());
    record.minimumSampleCount = query.value(2).toInt();
    record.strongSampleCount = query.value(3).toInt();
    record.stableCandidateCount = query.value(4).toInt();
    record.createdAtUtc = dateFromString(query.value(5).toString());
    return record;
}

std::optional<StabilityRunRecord> SessionStore::latestStabilityRun() const {
    clearReadError();
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(R"sql(
        SELECT stability_run_id, source_match_run_ids, minimum_sample_count, strong_sample_count,
               stable_candidate_count, created_at_utc
        FROM stability_runs
        ORDER BY created_at_utc DESC, stability_run_id DESC
        LIMIT 1
    )sql"))) {
        setReadError(QStringLiteral("读取最近稳定性运行失败"), query.lastError());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    StabilityRunRecord record;
    record.stabilityRunId = query.value(0).toString();
    record.sourceMatchRunIds = parseStringList(query.value(1).toString());
    record.minimumSampleCount = query.value(2).toInt();
    record.strongSampleCount = query.value(3).toInt();
    record.stableCandidateCount = query.value(4).toInt();
    record.createdAtUtc = dateFromString(query.value(5).toString());
    return record;
}

QList<StableCandidateRecord> SessionStore::stableCandidates(const QString& stabilityRunId) const {
    clearReadError();
    QList<StableCandidateRecord> candidates;
    QSqlQuery query(m_db);
    if (!query.prepare(QStringLiteral(R"sql(
        SELECT id, stability_run_id, rank_index, candidate_type, word_order, byte_order,
               slave_id, function_code, start_address, register_count, scale_multiplier, scale_offset,
               sample_count, meets_minimum_sample_count, confidence_level, run_ids, source_scan_session_ids,
               observation_ids, addresses, mean_target_value, mean_engineering_value, mean_absolute_error,
               max_absolute_error, mean_candidate_score, mean_error_quality, sample_quality, stability_score,
               evidence_summary
        FROM stable_candidates
        WHERE stability_run_id = :stability_run_id
        ORDER BY rank_index ASC, id ASC
    )sql"))) {
        setReadError(QStringLiteral("读取稳定候选失败"), query.lastError());
        return candidates;
    }
    query.bindValue(QStringLiteral(":stability_run_id"), stabilityRunId);
    if (!query.exec()) {
        setReadError(QStringLiteral("读取稳定候选失败"), query.lastError());
        return candidates;
    }

    while (query.next()) {
        StableCandidateRecord record;
        record.id = query.value(0).toLongLong();
        record.stabilityRunId = query.value(1).toString();
        record.rankIndex = query.value(2).toInt();
        record.candidateType = query.value(3).toString();
        record.wordOrder = query.value(4).toString();
        record.byteOrder = query.value(5).toString();
        record.slaveId = query.value(6).toInt();
        record.functionCode = query.value(7).toInt();
        record.startAddress = query.value(8).toInt();
        record.registerCount = query.value(9).toInt();
        record.scaleMultiplier = query.value(10).toDouble();
        record.scaleOffset = query.value(11).toDouble();
        record.sampleCount = query.value(12).toInt();
        record.meetsMinimumSampleCount = query.value(13).toInt() != 0;
        record.confidenceLevel = query.value(14).toString();
        record.runIds = parseStringList(query.value(15).toString());
        record.sourceScanSessionIds = parseStringList(query.value(16).toString());
        record.observationIds = parseLongList(query.value(17).toString());
        record.addresses = parseIntList(query.value(18).toString());
        record.meanTargetValue = query.value(19).toDouble();
        record.meanEngineeringValue = query.value(20).toDouble();
        record.meanAbsoluteError = query.value(21).toDouble();
        record.maxAbsoluteError = query.value(22).toDouble();
        record.meanCandidateScore = query.value(23).toDouble();
        record.meanErrorQuality = query.value(24).toDouble();
        record.sampleQuality = query.value(25).toDouble();
        record.stabilityScore = query.value(26).toDouble();
        record.evidenceSummary = query.value(27).toString();
        candidates.append(record);
    }
    return candidates;
}

} // namespace svm::storage
