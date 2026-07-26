#include "native_storage/native_store_file_ops.h"
#include "native_storage/native_store_files.h"
#include "native_storage/native_store_record_io.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace svm::native_storage::store_file_ops {
namespace {

constexpr std::string_view kTransactionManifestFileName = ".svm-store-transaction.svmr";
constexpr std::string_view kTransactionManifestTempFileName = ".svm-store-transaction.svmr.tmp";
constexpr std::string_view kTransactionCommitFileName = ".svm-store-transaction.commit";
constexpr std::string_view kTransactionCommitTempFileName = ".svm-store-transaction.commit.tmp";
constexpr std::string_view kTransactionManifestVersion = "1";

bool replacementFailureRequestedForTest(const std::filesystem::path& path) {
    const char* requestedFile = std::getenv("SVM_NATIVE_STORE_FAIL_REPLACE_FILE");
    return requestedFile != nullptr && path.filename().string() == requestedFile;
}

bool transactionInterruptionRequestedForTest(std::string_view phase) {
    const char* requestedPhase = std::getenv("SVM_NATIVE_STORE_INTERRUPT_TRANSACTION");
    return requestedPhase != nullptr && phase == requestedPhase;
}

void removeNoThrow(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

std::filesystem::path transactionManifestPath(const std::filesystem::path& storeDirectory) {
    return storeDirectory / kTransactionManifestFileName;
}

std::filesystem::path transactionManifestTempPath(const std::filesystem::path& storeDirectory) {
    return storeDirectory / kTransactionManifestTempFileName;
}

std::filesystem::path transactionCommitPath(const std::filesystem::path& storeDirectory) {
    return storeDirectory / kTransactionCommitFileName;
}

std::filesystem::path transactionCommitTempPath(const std::filesystem::path& storeDirectory) {
    return storeDirectory / kTransactionCommitTempFileName;
}

bool pathExists(const std::filesystem::path& path, bool& exists, std::string& errorText, std::string_view operation) {
    std::error_code error;
    exists = std::filesystem::exists(path, error);
    if (!error) {
        return true;
    }
    errorText = std::string(operation) + "失败：无法检查 " + path.filename().string() + "：" + error.message();
    return false;
}

bool removePath(const std::filesystem::path& path, std::string& errorText, std::string_view operation) {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (!error) {
        return true;
    }
    errorText = std::string(operation) + "失败：无法移除 " + path.filename().string() + "：" + error.message();
    return false;
}

bool validManagedFileName(std::string_view fileName) {
    if (fileName.empty() || !store_files::isStoreManagedFile(fileName)) {
        return false;
    }
    const std::filesystem::path path(fileName);
    return path == path.filename();
}

bool validateTargets(
    const std::filesystem::path& storeDirectory,
    const std::vector<ReplacementTarget>& targets,
    std::string& errorText,
    std::string_view operation) {
    if (targets.empty()) {
        errorText = std::string(operation) + "失败：事务目标为空。";
        return false;
    }

    std::vector<std::string> fileNames;
    fileNames.reserve(targets.size());
    for (const ReplacementTarget& target : targets) {
        const std::string fileName = target.path.filename().string();
        if (target.path.parent_path() != storeDirectory
            || !validManagedFileName(fileName)
            || target.tempPath != replacementTempPath(target.path)
            || target.backupPath != replacementBackupPath(target.path)) {
            errorText = std::string(operation) + "失败：事务目标路径无效。";
            return false;
        }
        if (std::find(fileNames.begin(), fileNames.end(), fileName) != fileNames.end()) {
            errorText = std::string(operation) + "失败：事务目标重复 " + fileName + "。";
            return false;
        }
        fileNames.push_back(fileName);
    }
    return true;
}

bool writeTransactionManifest(
    const std::filesystem::path& storeDirectory,
    const std::vector<ReplacementTarget>& targets,
    std::string& errorText,
    std::string_view operation) {
    const auto manifestPath = transactionManifestPath(storeDirectory);
    const auto tempPath = transactionManifestTempPath(storeDirectory);
    removeNoThrow(tempPath);

    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorText = std::string(operation) + "失败：无法创建事务清单。";
        return false;
    }
    output << store_io::kHeader;
    if (!store_io::writeRecord(output, {"transaction", std::string(kTransactionManifestVersion)})) {
        errorText = std::string(operation) + "失败：无法写入事务清单版本。";
        return false;
    }
    for (const ReplacementTarget& target : targets) {
        if (!store_io::writeRecord(output, {
                "target",
                target.path.filename().string(),
                target.hadPath ? "1" : "0",
            })) {
            errorText = std::string(operation) + "失败：无法写入事务目标。";
            return false;
        }
    }
    output.flush();
    output.close();
    if (!output) {
        errorText = std::string(operation) + "失败：事务清单写入中断。";
        return false;
    }

    std::error_code error;
    std::filesystem::rename(tempPath, manifestPath, error);
    if (error) {
        removeNoThrow(tempPath);
        errorText = std::string(operation) + "失败：无法发布事务清单：" + error.message();
        return false;
    }
    return true;
}

bool loadTransactionManifest(
    const std::filesystem::path& storeDirectory,
    std::vector<ReplacementTarget>& targets,
    std::string& errorText) {
    const auto manifestPath = transactionManifestPath(storeDirectory);
    std::ifstream input(manifestPath, std::ios::binary);
    if (!input) {
        errorText = "恢复 native 存储事务失败：无法打开事务清单。";
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        errorText = "恢复 native 存储事务失败：无法读取事务清单。";
        return false;
    }

    const std::string data = buffer.str();
    if (!data.starts_with(store_io::kHeader)) {
        errorText = "恢复 native 存储事务失败：事务清单文件头无效。";
        return false;
    }
    std::string parseError;
    const std::vector<store_io::Record> records = store_io::parseRecords(data, &parseError);
    if (!parseError.empty()
        || records.empty()
        || records.front() != store_io::Record{"transaction", std::string(kTransactionManifestVersion)}) {
        errorText = "恢复 native 存储事务失败：事务清单损坏。";
        return false;
    }

    targets.clear();
    targets.reserve(records.size() - 1);
    for (std::size_t index = 1; index < records.size(); ++index) {
        const store_io::Record& record = records[index];
        if (record.size() != 3
            || record[0] != "target"
            || !validManagedFileName(record[1])
            || (record[2] != "0" && record[2] != "1")) {
            errorText = "恢复 native 存储事务失败：事务目标损坏。";
            return false;
        }
        const auto path = storeDirectory / record[1];
        const auto duplicate = std::find_if(targets.begin(), targets.end(), [&](const ReplacementTarget& target) {
            return target.path == path;
        });
        if (duplicate != targets.end()) {
            errorText = "恢复 native 存储事务失败：事务目标重复。";
            return false;
        }
        targets.push_back(ReplacementTarget{
            replacementTempPath(path),
            path,
            replacementBackupPath(path),
            record[2] == "1",
        });
    }
    if (targets.empty()) {
        errorText = "恢复 native 存储事务失败：事务清单没有目标。";
        return false;
    }
    return true;
}

bool rollbackUncommittedTransaction(
    const std::filesystem::path& storeDirectory,
    const std::vector<ReplacementTarget>& targets,
    std::string& errorText) {
    for (const ReplacementTarget& target : targets) {
        bool hasPath = false;
        bool hasBackup = false;
        if (!pathExists(target.path, hasPath, errorText, "回滚 native 存储事务")
            || !pathExists(target.backupPath, hasBackup, errorText, "回滚 native 存储事务")) {
            return false;
        }
        if (target.hadPath && !hasPath && !hasBackup) {
            errorText = "回滚 native 存储事务失败：旧文件和备份均不存在 "
                + target.path.filename().string() + "。";
            return false;
        }
        if (!target.hadPath && hasBackup) {
            errorText = "回滚 native 存储事务失败：无旧文件的目标出现异常备份 "
                + target.backupPath.filename().string() + "。";
            return false;
        }
    }

    for (auto iterator = targets.rbegin(); iterator != targets.rend(); ++iterator) {
        const ReplacementTarget& target = *iterator;
        bool hasBackup = false;
        if (!pathExists(target.backupPath, hasBackup, errorText, "回滚 native 存储事务")) {
            return false;
        }
        if (hasBackup) {
            bool hasPath = false;
            if (!pathExists(target.path, hasPath, errorText, "回滚 native 存储事务")) {
                return false;
            }
            if (hasPath && !removePath(target.path, errorText, "回滚 native 存储事务")) {
                return false;
            }
            std::error_code error;
            std::filesystem::rename(target.backupPath, target.path, error);
            if (error) {
                errorText = "回滚 native 存储事务失败：无法还原 "
                    + target.path.filename().string() + "：" + error.message();
                return false;
            }
        } else if (!target.hadPath) {
            if (!removePath(target.path, errorText, "回滚 native 存储事务")) {
                return false;
            }
        }
        if (!removePath(target.tempPath, errorText, "回滚 native 存储事务")) {
            return false;
        }
    }

    if (!removePath(transactionManifestPath(storeDirectory), errorText, "回滚 native 存储事务")) {
        return false;
    }
    removeNoThrow(transactionManifestTempPath(storeDirectory));
    removeNoThrow(transactionCommitTempPath(storeDirectory));
    return true;
}

bool writeTransactionCommitMarker(
    const std::filesystem::path& storeDirectory,
    std::string& errorText,
    std::string_view operation) {
    const auto commitPath = transactionCommitPath(storeDirectory);
    const auto tempPath = transactionCommitTempPath(storeDirectory);
    removeNoThrow(tempPath);
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorText = std::string(operation) + "失败：无法创建事务提交标记。";
        return false;
    }
    output << "SVM_NATIVE_STORE_TRANSACTION_COMMITTED_V1\n";
    output.flush();
    output.close();
    if (!output) {
        errorText = std::string(operation) + "失败：事务提交标记写入中断。";
        return false;
    }
    std::error_code error;
    std::filesystem::rename(tempPath, commitPath, error);
    if (error) {
        removeNoThrow(tempPath);
        errorText = std::string(operation) + "失败：无法发布事务提交标记：" + error.message();
        return false;
    }
    return true;
}

bool finishCommittedTransaction(
    const std::filesystem::path& storeDirectory,
    const std::vector<ReplacementTarget>& targets,
    std::string& errorText) {
    for (const ReplacementTarget& target : targets) {
        bool hasPath = false;
        if (!pathExists(target.path, hasPath, errorText, "恢复已提交 native 存储事务")) {
            return false;
        }
        if (!hasPath) {
            errorText = "恢复已提交 native 存储事务失败：已提交文件不存在 "
                + target.path.filename().string() + "。";
            return false;
        }
    }

    for (const ReplacementTarget& target : targets) {
        if (!removePath(target.backupPath, errorText, "恢复已提交 native 存储事务")
            || !removePath(target.tempPath, errorText, "恢复已提交 native 存储事务")) {
            return false;
        }
    }

    // Removing the manifest first is safe only after every committed file and backup is settled.
    if (!removePath(transactionManifestPath(storeDirectory), errorText, "恢复已提交 native 存储事务")) {
        return false;
    }
    if (!removePath(transactionCommitPath(storeDirectory), errorText, "恢复已提交 native 存储事务")) {
        return false;
    }
    removeNoThrow(transactionManifestTempPath(storeDirectory));
    removeNoThrow(transactionCommitTempPath(storeDirectory));
    errorText.clear();
    return true;
}

bool abortTransaction(
    const std::filesystem::path& storeDirectory,
    const std::vector<ReplacementTarget>& targets,
    std::string& errorText) {
    const std::string primaryError = errorText;
    std::string rollbackError;
    if (!rollbackUncommittedTransaction(storeDirectory, targets, rollbackError)) {
        errorText = primaryError + "；回滚失败：" + rollbackError;
        return false;
    }
    errorText = primaryError;
    return false;
}

} // namespace

std::filesystem::path replacementTempPath(const std::filesystem::path& path) {
    return path.string() + ".tmp";
}

std::filesystem::path replacementBackupPath(const std::filesystem::path& path) {
    return path.string() + ".bak";
}

std::filesystem::path recoveryOrphanPath(const std::filesystem::path& path) {
    return path.string() + ".orphan";
}

bool recoverReplacementTransaction(const std::filesystem::path& storeDirectory, std::string& errorText) {
    const auto manifestPath = transactionManifestPath(storeDirectory);
    const auto commitPath = transactionCommitPath(storeDirectory);
    bool hasManifest = false;
    bool hasCommit = false;
    if (!pathExists(manifestPath, hasManifest, errorText, "恢复 native 存储事务")
        || !pathExists(commitPath, hasCommit, errorText, "恢复 native 存储事务")) {
        return false;
    }

    if (!hasManifest) {
        removeNoThrow(transactionManifestTempPath(storeDirectory));
        removeNoThrow(transactionCommitTempPath(storeDirectory));
        if (hasCommit && !removePath(commitPath, errorText, "恢复 native 存储事务")) {
            return false;
        }
        errorText.clear();
        return true;
    }

    std::vector<ReplacementTarget> targets;
    if (!loadTransactionManifest(storeDirectory, targets, errorText)
        || !validateTargets(storeDirectory, targets, errorText, "恢复 native 存储事务")) {
        return false;
    }
    if (hasCommit) {
        return finishCommittedTransaction(storeDirectory, targets, errorText);
    }
    if (!rollbackUncommittedTransaction(storeDirectory, targets, errorText)) {
        return false;
    }
    errorText.clear();
    return true;
}

bool recoverReplacementArtifacts(const std::filesystem::path& path, std::string& errorText) {
    const auto backupPath = replacementBackupPath(path);
    const auto tempPath = replacementTempPath(path);
    std::error_code error;
    const bool hasPath = std::filesystem::exists(path, error);
    if (error) {
        errorText = "恢复 native 存储替换状态失败：无法检查 " + path.filename().string() + "：" + error.message();
        return false;
    }

    const bool hasBackup = std::filesystem::exists(backupPath, error);
    if (error) {
        errorText = "恢复 native 存储替换状态失败：无法检查备份文件 " + backupPath.filename().string() + "：" + error.message();
        return false;
    }

    if (hasBackup) {
        if (hasPath) {
            std::filesystem::remove(path, error);
            if (error) {
                errorText = "恢复 native 存储替换状态失败：无法移除未提交文件 "
                    + path.filename().string() + "：" + error.message();
                return false;
            }
        }
        std::filesystem::rename(backupPath, path, error);
        if (error) {
            errorText = "恢复 native 存储替换状态失败：无法还原备份 " + backupPath.filename().string() + "：" + error.message();
            return false;
        }
    }

    if (std::filesystem::exists(tempPath, error)) {
        std::filesystem::remove(tempPath, error);
        if (error) {
            errorText = "恢复 native 存储替换状态失败：无法清理临时文件 " + tempPath.filename().string() + "：" + error.message();
            return false;
        }
    } else if (error) {
        errorText = "恢复 native 存储替换状态失败：无法检查临时文件 " + tempPath.filename().string() + "：" + error.message();
        return false;
    }

    errorText.clear();
    return true;
}

bool replaceFileWithTemp(
    const std::filesystem::path& tempPath,
    const std::filesystem::path& path,
    std::string& errorText,
    std::string_view operation) {
    const auto backupPath = replacementBackupPath(path);
    std::error_code error;
    const bool hasPath = std::filesystem::exists(path, error);
    if (error) {
        errorText = std::string(operation) + "失败：无法检查旧文件：" + error.message();
        return false;
    }

    if (hasPath) {
        if (std::filesystem::exists(backupPath, error)) {
            std::filesystem::remove(backupPath, error);
            if (error) {
                errorText = std::string(operation) + "失败：无法清理旧备份：" + error.message();
                return false;
            }
        } else if (error) {
            errorText = std::string(operation) + "失败：无法检查旧备份：" + error.message();
            return false;
        }

        std::filesystem::rename(path, backupPath, error);
        if (error) {
            std::filesystem::remove(tempPath);
            errorText = std::string(operation) + "失败：无法备份旧文件：" + error.message();
            return false;
        }
    }

    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::error_code cleanupError;
        if (hasPath) {
            std::filesystem::rename(backupPath, path, cleanupError);
        }
        std::filesystem::remove(tempPath);
        if (hasPath && cleanupError) {
            errorText = std::string(operation) + "失败：替换临时文件失败：" + error.message()
                + "；恢复旧文件也失败：" + cleanupError.message();
            return false;
        }
        errorText = std::string(operation) + "失败：替换临时文件失败：" + error.message();
        return false;
    }

    if (hasPath) {
        std::filesystem::remove(backupPath, error);
        if (error) {
            errorText = std::string(operation) + "已完成，但清理旧备份失败：" + error.message();
            return false;
        }
    }

    errorText.clear();
    return true;
}

bool commitReplacementTargets(std::vector<ReplacementTarget>& targets, std::string& errorText, std::string_view operation) {
    if (targets.empty()) {
        errorText.clear();
        return true;
    }
    const std::filesystem::path storeDirectory = targets.front().path.parent_path();
    if (!validateTargets(storeDirectory, targets, errorText, operation)) {
        return false;
    }

    bool hasManifest = false;
    bool hasCommit = false;
    if (!pathExists(transactionManifestPath(storeDirectory), hasManifest, errorText, operation)
        || !pathExists(transactionCommitPath(storeDirectory), hasCommit, errorText, operation)) {
        return false;
    }
    if (hasManifest || hasCommit) {
        errorText = std::string(operation) + "失败：存在尚未恢复的存储事务。";
        return false;
    }

    for (ReplacementTarget& target : targets) {
        bool hasTemp = false;
        bool hasBackup = false;
        if (!pathExists(target.path, target.hadPath, errorText, operation)
            || !pathExists(target.tempPath, hasTemp, errorText, operation)
            || !pathExists(target.backupPath, hasBackup, errorText, operation)) {
            return false;
        }
        if (!hasTemp) {
            errorText = std::string(operation) + "失败：事务临时文件不存在 "
                + target.tempPath.filename().string() + "。";
            return false;
        }
        if (hasBackup) {
            errorText = std::string(operation) + "失败：存在未恢复的旧备份 "
                + target.backupPath.filename().string() + "。";
            return false;
        }
    }

    if (!writeTransactionManifest(storeDirectory, targets, errorText, operation)) {
        return false;
    }

    for (ReplacementTarget& target : targets) {
        if (target.hadPath) {
            std::error_code error;
            std::filesystem::rename(target.path, target.backupPath, error);
            if (error) {
                errorText = std::string(operation) + "失败：无法备份旧文件 "
                    + target.path.filename().string() + "：" + error.message();
                return abortTransaction(storeDirectory, targets, errorText);
            }
        }
    }

    for (ReplacementTarget& target : targets) {
        if (replacementFailureRequestedForTest(target.path)) {
            errorText = std::string(operation) + "失败：测试注入替换失败 "
                + target.path.filename().string() + "。";
            return abortTransaction(storeDirectory, targets, errorText);
        }

        std::error_code error;
        std::filesystem::rename(target.tempPath, target.path, error);
        if (error) {
            errorText = std::string(operation) + "失败：替换临时文件 "
                + target.tempPath.filename().string() + "：" + error.message();
            return abortTransaction(storeDirectory, targets, errorText);
        }
    }

    if (transactionInterruptionRequestedForTest("before_commit")) {
        errorText = std::string(operation) + "失败：测试注入提交标记前中断。";
        return false;
    }
    if (!writeTransactionCommitMarker(storeDirectory, errorText, operation)) {
        return abortTransaction(storeDirectory, targets, errorText);
    }

    bool cleanedBackup = false;
    for (ReplacementTarget& target : targets) {
        std::error_code error;
        std::filesystem::remove(target.backupPath, error);
        if (error) {
            errorText.clear();
            return true;
        }
        cleanedBackup = cleanedBackup || target.hadPath;
        if (cleanedBackup && transactionInterruptionRequestedForTest("during_cleanup")) {
            errorText = std::string(operation) + "失败：测试注入提交后清理中断。";
            return false;
        }
    }

    std::string cleanupError;
    if (!finishCommittedTransaction(storeDirectory, targets, cleanupError)) {
        errorText.clear();
        return true;
    }

    errorText.clear();
    return true;
}

} // namespace svm::native_storage::store_file_ops
