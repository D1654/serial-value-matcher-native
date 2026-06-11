#pragma once

#include "matching/value_candidate_generator.h"
#include "storage/scan_persistence_records.h"

#include <QList>

namespace svm::matching {

QList<RegisterSample> registerSamplesFromScanObservations(const QList<svm::storage::ScanObservationRecord>& records);

} // namespace svm::matching
