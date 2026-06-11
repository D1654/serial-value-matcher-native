#pragma once

#include <QString>

namespace svm::report {

struct TextFileWriteResult {
    bool success = false;
    QString errorMessage;
};

TextFileWriteResult writeUtf8TextFile(const QString& filePath, const QString& text);

} // namespace svm::report
