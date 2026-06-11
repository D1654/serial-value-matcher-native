#pragma once

#include <QList>
#include <QString>

#include "storage/rule_verification_persistence_records.h"

namespace svm::report {

QString renderRuleVerificationMarkdownReport(
    const svm::storage::RuleVerificationRunRecord& run,
    const QList<svm::storage::RuleVerificationResultRecord>& results);

} // namespace svm::report
