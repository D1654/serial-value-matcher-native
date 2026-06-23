#pragma once

#include "native_storage/native_session_store.h"

#include <cstdint>
#include <string>

namespace svm::native_storage::store_records {

std::string toString(std::int64_t value);
std::int64_t toInt64(const std::string& value, std::int64_t fallback = 0);

RawIoEvent rawEventFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromRawEvent(const RawIoEvent& event);
SendHistoryEntry sendHistoryFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromSendHistory(const SendHistoryEntry& entry);
SerialProfile serialProfileFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromSerialProfile(const SerialProfile& profile);
UiPreferences uiPreferencesFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromUiPreferences(const UiPreferences& preferences);
ScanSessionRecord scanSessionFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromScanSession(const ScanSessionRecord& session);
ScanAttemptRecord scanAttemptFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromScanAttempt(const ScanAttemptRecord& attempt);
ScanObservationRecord scanObservationFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromScanObservation(const ScanObservationRecord& observation);
MatchRunRecord matchRunFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromMatchRun(const MatchRunRecord& run);
MatchCandidateRecord matchCandidateFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromMatchCandidate(const MatchCandidateRecord& candidate);
ProtocolFieldRuleRecord protocolRuleFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromProtocolRule(const ProtocolFieldRuleRecord& rule);
RuleVerificationRunRecord verificationRunFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromVerificationRun(const RuleVerificationRunRecord& run);
RuleVerificationResultRecord verificationResultFromRecord(const NativeSessionStore::Record& record);
NativeSessionStore::Record recordFromVerificationResult(const RuleVerificationResultRecord& result);

} // namespace svm::native_storage::store_records
