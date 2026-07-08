#pragma once

#include <concepts>
#include <optional>
#include <string_view>
#include <type_traits>

namespace svm::storage {

enum class SessionStoreBackendKind {
    QtSql,
    NativeFile,
};

struct SessionStorePortDescriptor {
    std::string_view name;
    SessionStoreBackendKind backendKind = SessionStoreBackendKind::QtSql;
    bool supportsRawIo = false;
    bool supportsRawRetention = false;
    bool supportsSendHistory = false;
    bool supportsSerialProfiles = false;
    bool supportsUiPreferences = false;
    bool supportsModbusScans = false;
    bool supportsMatchRuns = false;
    bool supportsProtocolRules = false;
    bool supportsRuleVerification = false;
    bool exposesBackendFiles = false;
};

template <typename Store>
struct SessionStorePortTraits;

template <typename Store>
using SessionStorePortTraitsFor = SessionStorePortTraits<std::remove_cvref_t<Store>>;

template <typename Store>
concept SessionStorePortDescriptorProvider = requires {
    SessionStorePortTraitsFor<Store>::descriptor;
    typename SessionStorePortTraitsFor<Store>::OpenLocation;
    typename SessionStorePortTraitsFor<Store>::ErrorText;
};

template <typename Store>
concept SessionStoreLifecyclePort =
    SessionStorePortDescriptorProvider<Store> &&
    requires(
        Store& store,
        const typename SessionStorePortTraitsFor<Store>::OpenLocation& location) {
        { store.open(location) } -> std::same_as<bool>;
        { store.initializeSchema() } -> std::same_as<bool>;
        { store.lastErrorText() } -> std::convertible_to<typename SessionStorePortTraitsFor<Store>::ErrorText>;
    };

template <typename Store>
concept SessionStoreOpenStatePort =
    SessionStorePortDescriptorProvider<Store> &&
    requires(const Store& store) {
        { store.isOpen() } -> std::same_as<bool>;
    };

template <typename Store>
concept SessionStoreReadDiagnosticsPort =
    SessionStorePortDescriptorProvider<Store> &&
    requires(const Store& store) {
        { store.hasReadError() } -> std::same_as<bool>;
        { store.lastReadErrorText() } -> std::convertible_to<typename SessionStorePortTraitsFor<Store>::ErrorText>;
        { store.clearReadError() } -> std::same_as<void>;
    };

template <typename Store>
concept SessionStoreRawIoPort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        const typename SessionStorePortTraitsFor<Store>::RawIoEvent& event,
        const typename SessionStorePortTraitsFor<Store>::RawIoEventBatch& events) {
        { store.appendRawEvent(event) } -> std::same_as<bool>;
        { store.appendRawEvents(events) } -> std::same_as<bool>;
        { constStore.rawEventCount() } -> std::convertible_to<typename SessionStorePortTraitsFor<Store>::RawIoCount>;
    };

template <typename Store>
concept SessionStoreRecentRawIoPort =
    SessionStoreRawIoPort<Store> &&
    requires(const Store& store) {
        typename SessionStorePortTraitsFor<Store>::RawIoEventList;
        { store.recentRawEvents(1) } -> std::same_as<typename SessionStorePortTraitsFor<Store>::RawIoEventList>;
    };

template <typename Store>
concept SessionStoreRawRetentionPort =
    SessionStoreRawIoPort<Store> &&
    requires(Store& store) {
        store.setRawEventRetentionLimit(1024U, 512U);
    };

template <typename Store>
concept SessionStoreSerialProfilePort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        const typename SessionStorePortTraitsFor<Store>::SerialProfile& profile) {
        { store.saveSerialProfile(profile) } -> std::same_as<bool>;
        { constStore.latestSerialProfile() } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::SerialProfile>>;
    };

template <typename Store>
concept SessionStoreUiPreferencesPort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        const typename SessionStorePortTraitsFor<Store>::UiPreferences& preferences) {
        { store.saveUiPreferences(preferences) } -> std::same_as<bool>;
        { constStore.latestUiPreferences() } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::UiPreferences>>;
    };

template <typename Store>
concept SessionStoreScanPort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        int limit,
        const typename SessionStorePortTraitsFor<Store>::ScanExecution& execution,
        const typename SessionStorePortTraitsFor<Store>::ScanSessionId& sessionId) {
        { store.saveScanExecution(execution) } -> std::same_as<bool>;
        { constStore.latestScanSession() } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::ScanSession>>;
        { constStore.scanSession(sessionId) } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::ScanSession>>;
        { constStore.recentScanSessions(limit) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::ScanSessionList>;
        { constStore.scanAttempts(sessionId) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::ScanAttemptList>;
        { constStore.scanObservations(sessionId) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::ScanObservationList>;
    };

template <typename Store>
concept SessionStoreMatchPort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        int limit,
        const typename SessionStorePortTraitsFor<Store>::MatchRun& run,
        const typename SessionStorePortTraitsFor<Store>::MatchRunId& runId,
        const typename SessionStorePortTraitsFor<Store>::MatchCandidatesInput& candidates) {
        { store.saveMatchRun(run, candidates) } -> std::same_as<bool>;
        { constStore.matchRun(runId) } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::MatchRun>>;
        { constStore.recentMatchRuns(limit) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::MatchRunList>;
        { constStore.matchCandidates(runId) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::MatchCandidateList>;
    };

template <typename Store>
concept SessionStoreProtocolRulePort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        int limit,
        const typename SessionStorePortTraitsFor<Store>::ProtocolFieldRule& rule,
        const typename SessionStorePortTraitsFor<Store>::RuleId& ruleId) {
        { store.saveProtocolFieldRule(rule) } -> std::same_as<bool>;
        { store.deleteProtocolFieldRule(ruleId) } -> std::same_as<bool>;
        { constStore.protocolFieldRule(ruleId) } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::ProtocolFieldRule>>;
        { constStore.recentProtocolFieldRules(limit) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::ProtocolFieldRuleList>;
    };

template <typename Store>
concept SessionStoreRuleVerificationPort =
    SessionStoreLifecyclePort<Store> &&
    requires(
        Store& store,
        const Store& constStore,
        const typename SessionStorePortTraitsFor<Store>::RuleVerificationRun& run,
        const typename SessionStorePortTraitsFor<Store>::RuleVerificationRunId& verificationRunId,
        const typename SessionStorePortTraitsFor<Store>::RuleVerificationResultsInput& results) {
        { store.saveRuleVerificationRun(run, results) } -> std::same_as<bool>;
        { constStore.latestRuleVerificationRun() } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::RuleVerificationRun>>;
        { constStore.ruleVerificationRun(verificationRunId) } ->
            std::same_as<std::optional<typename SessionStorePortTraitsFor<Store>::RuleVerificationRun>>;
        { constStore.ruleVerificationResults(verificationRunId) } ->
            std::same_as<typename SessionStorePortTraitsFor<Store>::RuleVerificationResultList>;
    };

template <typename Store>
concept SessionStorePort =
    SessionStoreRawIoPort<Store> &&
    SessionStoreSerialProfilePort<Store> &&
    SessionStoreScanPort<Store> &&
    SessionStoreMatchPort<Store> &&
    SessionStoreProtocolRulePort<Store> &&
    SessionStoreRuleVerificationPort<Store>;

} // namespace svm::storage
