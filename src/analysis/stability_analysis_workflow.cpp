#include "analysis/stability_analysis_workflow.h"

#include <QDateTime>
#include <QStringList>
#include <QUuid>

#include "matching/candidate_stability_analyzer.h"

namespace svm::analysis {

namespace {

QString newStabilityRunId()
{
    return QStringLiteral("stability-%1-%2")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmsszzz")))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
}

} // namespace

StabilityWorkflowResult runRecentMatchStabilityAnalysis(
    storage::SessionStore& store,
    StabilityWorkflowOptions options)
{
    StabilityWorkflowResult workflow;
    if (options.matchRunLimit <= 0) {
        options.matchRunLimit = 1;
    }
    if (options.minimumSampleCount <= 0) {
        workflow.errorMessage = QStringLiteral("稳定性分析失败：最少样本数必须大于 0。");
        return workflow;
    }
    if (options.strongSampleCount < options.minimumSampleCount) {
        options.strongSampleCount = options.minimumSampleCount;
    }

    const auto runs = store.recentMatchRuns(options.matchRunLimit);
    if (store.hasReadError()) {
        workflow.errorMessage = store.lastReadErrorText();
        return workflow;
    }
    if (runs.isEmpty()) {
        workflow.errorMessage = QStringLiteral("暂无匹配运行。请先基于扫描会话生成候选，再执行稳定性分析。");
        return workflow;
    }

    QStringList sourceRunIds;
    QList<matching::CandidateObservation> observations;
    for (const storage::MatchRunRecord& run : runs) {
        const auto candidates = store.matchCandidates(run.runId);
        if (store.hasReadError()) {
            workflow.errorMessage = store.lastReadErrorText();
            return workflow;
        }
        if (candidates.isEmpty()) {
            continue;
        }
        sourceRunIds.append(run.runId);
        observations.append(matching::candidateObservationsFromMatchRecords(run, candidates));
    }

    if (sourceRunIds.size() < options.minimumSampleCount || observations.isEmpty()) {
        workflow.errorMessage = QStringLiteral("可用于稳定性分析的匹配运行不足：当前 %1 次，至少需要 %2 次。请在不同真实目标值或不同时间点继续生成候选。")
            .arg(sourceRunIds.size())
            .arg(options.minimumSampleCount);
        return workflow;
    }

    matching::StabilityAnalysisOptions analysisOptions;
    analysisOptions.minimumSampleCount = options.minimumSampleCount;
    analysisOptions.strongSampleCount = options.strongSampleCount;
    const matching::StabilityAnalysisResult analysis = matching::analyzeCandidateStability(observations, analysisOptions);
    if (!analysis.success) {
        workflow.errorMessage = analysis.errorMessage;
        return workflow;
    }

    storage::StabilityRunRecord stabilityRun;
    stabilityRun.stabilityRunId = newStabilityRunId();
    stabilityRun.sourceMatchRunIds = sourceRunIds;
    stabilityRun.minimumSampleCount = analysisOptions.minimumSampleCount;
    stabilityRun.strongSampleCount = analysisOptions.strongSampleCount;
    stabilityRun.createdAtUtc = QDateTime::currentDateTimeUtc();
    if (!store.saveStabilityRun(stabilityRun, analysis.candidates)) {
        workflow.errorMessage = store.lastErrorText();
        return workflow;
    }

    workflow.success = true;
    workflow.stabilityRunId = stabilityRun.stabilityRunId;
    workflow.sourceMatchRunCount = sourceRunIds.size();
    workflow.candidateObservationCount = observations.size();
    workflow.stableCandidateCount = analysis.candidates.size();
    return workflow;
}

} // namespace svm::analysis
