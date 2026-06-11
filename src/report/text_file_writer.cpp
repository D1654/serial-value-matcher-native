#include "report/text_file_writer.h"

#include <QIODevice>
#include <QSaveFile>

namespace svm::report {
namespace {

QString fileErrorDetail(const QSaveFile& file)
{
    const QString detail = file.errorString().trimmed();
    return detail.isEmpty() ? QStringLiteral("未知文件错误") : detail;
}

} // namespace

TextFileWriteResult writeUtf8TextFile(const QString& filePath, const QString& text)
{
    if (filePath.trimmed().isEmpty()) {
        return {false, QStringLiteral("文件路径不能为空。")};
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {false, QStringLiteral("无法打开文件 %1：%2。").arg(filePath, fileErrorDetail(file))};
    }

    const QByteArray bytes = text.toUtf8();
    const qint64 expectedBytes = static_cast<qint64>(bytes.size());
    const qint64 writtenBytes = file.write(bytes);
    if (writtenBytes != expectedBytes) {
        const QString detail = fileErrorDetail(file);
        file.cancelWriting();
        return {false, QStringLiteral("文件写入不完整 %1：预期 %2 字节，实际 %3 字节；%4。")
            .arg(filePath)
            .arg(expectedBytes)
            .arg(writtenBytes)
            .arg(detail)};
    }

    if (!file.commit()) {
        return {false, QStringLiteral("无法完成文件写入 %1：%2。").arg(filePath, fileErrorDetail(file))};
    }

    return {true, QString()};
}

} // namespace svm::report
