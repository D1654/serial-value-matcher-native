#include "matching/scan_observation_adapter.h"

namespace svm::matching {

QList<RegisterSample> registerSamplesFromScanObservations(const QList<svm::storage::ScanObservationRecord>& records)
{
    QList<RegisterSample> samples;
    samples.reserve(records.size());
    for (const svm::storage::ScanObservationRecord& record : records) {
        RegisterSample sample;
        sample.observationId = record.id;
        sample.sessionId = record.sessionId;
        sample.slaveId = record.slaveId;
        sample.functionCode = record.functionCode;
        sample.address = record.address;
        sample.value = static_cast<quint16>(record.value & 0xFFFF);
        sample.blockIndex = record.blockIndex;
        sample.attemptIndex = record.attemptIndex;
        sample.observedAtUtc = record.observedAtUtc;
        samples.append(sample);
    }
    return samples;
}

} // namespace svm::matching
