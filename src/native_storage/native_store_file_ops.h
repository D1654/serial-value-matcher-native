#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace svm::native_storage::store_file_ops {

struct ReplacementTarget {
    std::filesystem::path tempPath;
    std::filesystem::path path;
    std::filesystem::path backupPath;
    bool hadPath = false;
    bool backedUp = false;
    bool replaced = false;
};

std::filesystem::path replacementTempPath(const std::filesystem::path& path);
std::filesystem::path replacementBackupPath(const std::filesystem::path& path);
std::filesystem::path recoveryOrphanPath(const std::filesystem::path& path);
bool recoverReplacementArtifacts(const std::filesystem::path& path, std::string& errorText);
bool replaceFileWithTemp(
    const std::filesystem::path& tempPath,
    const std::filesystem::path& path,
    std::string& errorText,
    std::string_view operation);
bool commitReplacementTargets(
    std::vector<ReplacementTarget>& targets,
    std::string& errorText,
    std::string_view operation);

} // namespace svm::native_storage::store_file_ops
