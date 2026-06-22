#include "native_storage/native_store_file_ops.h"

#include <cstdlib>
#include <system_error>

namespace svm::native_storage::store_file_ops {
namespace {

bool replacementFailureRequestedForTest(const std::filesystem::path& path) {
    const char* requestedFile = std::getenv("SVM_NATIVE_STORE_FAIL_REPLACE_FILE");
    return requestedFile != nullptr && path.filename().string() == requestedFile;
}

void removeNoThrow(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

bool rollbackReplacementTargets(std::vector<ReplacementTarget>& targets, std::string& rollbackErrorText) {
    bool ok = true;
    for (ReplacementTarget& target : targets) {
        if (target.replaced) {
            std::error_code error;
            std::filesystem::remove(target.path, error);
            if (error) {
                rollbackErrorText = "无法移除未完成的新文件 " + target.path.filename().string() + "：" + error.message();
                ok = false;
            }
            target.replaced = false;
        }
    }

    for (auto iterator = targets.rbegin(); iterator != targets.rend(); ++iterator) {
        ReplacementTarget& target = *iterator;
        if (!target.backedUp) {
            continue;
        }
        std::error_code error;
        std::filesystem::rename(target.backupPath, target.path, error);
        if (error) {
            rollbackErrorText = "无法还原旧文件 " + target.path.filename().string() + "：" + error.message();
            ok = false;
        }
        target.backedUp = false;
    }

    for (const ReplacementTarget& target : targets) {
        removeNoThrow(target.tempPath);
    }
    return ok;
}

} // namespace

std::filesystem::path replacementTempPath(const std::filesystem::path& path) {
    return path.string() + ".tmp";
}

std::filesystem::path replacementBackupPath(const std::filesystem::path& path) {
    return path.string() + ".bak";
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

    if (!hasPath && hasBackup) {
        std::filesystem::rename(backupPath, path, error);
        if (error) {
            errorText = "恢复 native 存储替换状态失败：无法还原备份 " + backupPath.filename().string() + "：" + error.message();
            return false;
        }
    } else if (hasPath && hasBackup) {
        std::filesystem::remove(backupPath, error);
        if (error) {
            errorText = "恢复 native 存储替换状态失败：无法清理旧备份 " + backupPath.filename().string() + "：" + error.message();
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
    for (ReplacementTarget& target : targets) {
        std::error_code error;
        target.hadPath = std::filesystem::exists(target.path, error);
        if (error) {
            errorText = std::string(operation) + "失败：无法检查旧文件 "
                + target.path.filename().string() + "：" + error.message();
            std::string rollbackError;
            rollbackReplacementTargets(targets, rollbackError);
            return false;
        }

        if (std::filesystem::exists(target.backupPath, error)) {
            std::filesystem::remove(target.backupPath, error);
            if (error) {
                errorText = std::string(operation) + "失败：无法清理旧备份 "
                    + target.backupPath.filename().string() + "：" + error.message();
                std::string rollbackError;
                rollbackReplacementTargets(targets, rollbackError);
                return false;
            }
        } else if (error) {
            errorText = std::string(operation) + "失败：无法检查旧备份 "
                + target.backupPath.filename().string() + "：" + error.message();
            std::string rollbackError;
            rollbackReplacementTargets(targets, rollbackError);
            return false;
        }

        if (target.hadPath) {
            std::filesystem::rename(target.path, target.backupPath, error);
            if (error) {
                errorText = std::string(operation) + "失败：无法备份旧文件 "
                    + target.path.filename().string() + "：" + error.message();
                std::string rollbackError;
                rollbackReplacementTargets(targets, rollbackError);
                return false;
            }
            target.backedUp = true;
        }
    }

    for (ReplacementTarget& target : targets) {
        if (replacementFailureRequestedForTest(target.path)) {
            errorText = std::string(operation) + "失败：测试注入替换失败 "
                + target.path.filename().string() + "。";
            std::string rollbackError;
            if (!rollbackReplacementTargets(targets, rollbackError) && !rollbackError.empty()) {
                errorText += "；回滚失败：" + rollbackError;
            }
            return false;
        }

        std::error_code error;
        std::filesystem::rename(target.tempPath, target.path, error);
        if (error) {
            errorText = std::string(operation) + "失败：替换临时文件 "
                + target.tempPath.filename().string() + "：" + error.message();
            std::string rollbackError;
            if (!rollbackReplacementTargets(targets, rollbackError) && !rollbackError.empty()) {
                errorText += "；回滚失败：" + rollbackError;
            }
            return false;
        }
        target.replaced = true;
    }

    for (ReplacementTarget& target : targets) {
        if (!target.backedUp) {
            continue;
        }
        std::error_code error;
        std::filesystem::remove(target.backupPath, error);
        if (error) {
            errorText = std::string(operation) + "已完成，但清理旧备份 "
                + target.backupPath.filename().string() + " 失败：" + error.message();
            return false;
        }
        target.backedUp = false;
    }

    errorText.clear();
    return true;
}

} // namespace svm::native_storage::store_file_ops
