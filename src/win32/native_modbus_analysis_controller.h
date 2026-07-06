#pragma once

#include "win32/native_modbus_scan_ui_state.h"
#include "win32/native_serial_io_state.h"
#include "win32/native_ui_preferences.h"

#include <cstddef>
#include <cstdint>

namespace svm::win32 {

enum class NativeModbusAction {
    StartScan,
    CancelScan,
    ConnectRequired,
    StorageUnavailable,
    SerialBusy,
};

struct NativeModbusScanDecision {
    NativeModbusAction action = NativeModbusAction::SerialBusy;
    NativeSerialIoOwner owner = NativeSerialIoOwner::None;

    bool startsScan() const noexcept;
    bool cancelsScan() const noexcept;
};

enum class NativeAnalysisAction {
    RunAnalysis,
    StorageUnavailable,
    ScanRequired,
    ObservationsRequired,
    InvalidTarget,
    NoCandidates,
};

enum class NativeRuleAction {
    SaveCandidateRule,
    RunVerification,
    StorageUnavailable,
    CandidateOrRuleRequired,
    ScanRequired,
    RulesRequired,
    ObservationsRequired,
};

enum class NativeReportAction {
    Export,
    StorageUnavailable,
    RunRequired,
};

struct NativePreferenceSaveDecision {
    bool shouldSave = false;
    bool skipBecauseMinimized = false;
};

class NativeModbusAnalysisController final {
public:
    NativeModbusScanDecision scanDecision(
        bool scanRunning,
        bool serialOpen,
        bool storeOpen,
        const NativeSerialIoState& ioState) const noexcept;
    NativeModbusScanUiSnapshot scanUiSnapshot(const NativeModbusScanUiState& state, bool running) const noexcept;

    NativeAnalysisAction analysisDecision(
        bool storeOpen,
        bool hasScanSession,
        bool hasObservations,
        bool targetValid,
        bool hasCandidates) const noexcept;

    NativeRuleAction ruleVerificationDecision(
        bool storeOpen,
        bool hasSelectedCandidate,
        bool hasExistingRules,
        bool hasScanSession,
        bool hasRulesForVerification,
        bool hasObservations) const noexcept;

    NativeReportAction reportDecision(bool storeOpen, bool hasRun) const noexcept;

    NativePreferenceSaveDecision preferenceSaveDecision(bool storeOpen, bool windowReady, bool minimized) const noexcept;
    int normalizedRawEventRetentionLimitMb(int retentionLimitMb) const noexcept;
    std::uintmax_t rawEventSoftLimitBytes(int retentionLimitMb) const noexcept;
    std::uintmax_t rawEventTargetLimitBytes(int retentionLimitMb) const noexcept;
    bool shouldPersistPreferences(
        const native_storage::UiPreferences* lastSaved,
        const native_storage::UiPreferences& next) const;
};

} // namespace svm::win32
