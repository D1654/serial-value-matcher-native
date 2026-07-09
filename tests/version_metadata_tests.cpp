#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef SVM_SOURCE_ROOT
#error "SVM_SOURCE_ROOT must be provided by CMake."
#endif

#ifndef SVM_BINARY_DIR
#error "SVM_BINARY_DIR must be provided by CMake."
#endif

namespace {

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input && "required metadata file must exist");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::map<std::string, std::string> readVersionMetadata(const std::filesystem::path& root) {
    const std::string text = readText(root / "cmake" / "svm_version.cmake");
    const std::regex pattern(R"REGEX(^\s*set\(\s*([A-Za-z0-9_]+)\s+"([^"]*)"\s*\))REGEX");
    std::map<std::string, std::string> metadata;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_match(line, match, pattern)) {
            metadata.emplace(match[1].str(), match[2].str());
        }
    }
    return metadata;
}

void requiredMetadataKeysExist(const std::map<std::string, std::string>& metadata) {
    const std::vector<std::string> keys{
        "SVM_VERSION",
        "SVM_VERSION_MAJOR",
        "SVM_VERSION_MINOR",
        "SVM_VERSION_PATCH",
        "SVM_VERSION_TWEAK",
        "SVM_RELEASE_TAG",
        "SVM_PRODUCT_DISPLAY_NAME",
        "SVM_PRODUCT_REPOSITORY_NAME",
        "SVM_PRODUCT_AUTHOR",
        "SVM_PRODUCT_DESCRIPTION",
        "SVM_WIN32_FILE_DESCRIPTION",
        "SVM_WIN32_INTERNAL_NAME",
        "SVM_WIN32_EXE_NAME",
        "SVM_PACKAGE_ARTIFACT",
        "SVM_MINGW_PACKAGE_ARTIFACT",
        "SVM_RELEASE_URL",
    };
    for (const std::string& key : keys) {
        const auto found = metadata.find(key);
        assert(found != metadata.end());
        assert(!found->second.empty());
    }
}

void versionComponentsMatch(const std::map<std::string, std::string>& metadata) {
    const std::string version = metadata.at("SVM_VERSION");
    const std::string componentVersion = metadata.at("SVM_VERSION_MAJOR")
        + "." + metadata.at("SVM_VERSION_MINOR")
        + "." + metadata.at("SVM_VERSION_PATCH");
    assert(version == componentVersion);
    assert(metadata.at("SVM_VERSION_TWEAK") == "0");
    assert(metadata.at("SVM_RELEASE_TAG") == "v" + version);
    assert(contains(metadata.at("SVM_RELEASE_URL"), metadata.at("SVM_RELEASE_TAG")));
}

void cmakeConsumesVersionSource(const std::filesystem::path& root) {
    const std::string cmake = readText(root / "CMakeLists.txt");
    assert(contains(cmake, "include(cmake/svm_version.cmake)"));
    assert(contains(cmake, "VERSION \"${SVM_VERSION}\""));
    assert(!contains(cmake, "VERSION 1.0.0"));
    assert(contains(cmake, "cmake/svm_version_resource.h.in"));
    assert(contains(cmake, "generated/svm_version_resource.h"));
    assert(contains(cmake, "version_metadata_tests"));
}

void generatedHeaderMatchesSource(
    const std::filesystem::path& buildRoot,
    const std::map<std::string, std::string>& metadata) {
    const std::string header = readText(buildRoot / "generated" / "svm_version_resource.h");
    const std::string resourceVersion = metadata.at("SVM_VERSION_MAJOR")
        + "," + metadata.at("SVM_VERSION_MINOR")
        + "," + metadata.at("SVM_VERSION_PATCH")
        + "," + metadata.at("SVM_VERSION_TWEAK");
    assert(contains(header, "#define SVM_VERSION_FILE_VERSION " + resourceVersion));
    assert(contains(header, "#define SVM_VERSION_PRODUCT_VERSION " + resourceVersion));
    assert(contains(header, "#define SVM_VERSION_STRING \"" + metadata.at("SVM_VERSION") + "\""));
    assert(contains(header, "#define SVM_VERSION_ORIGINAL_FILENAME_STR \"" + metadata.at("SVM_WIN32_EXE_NAME") + "\""));
}

void win32ResourceUsesGeneratedHeader(const std::filesystem::path& root) {
    const std::string rc = readText(root / "src" / "win32" / "app.rc");
    assert(contains(rc, "#include \"svm_version_resource.h\""));
    assert(contains(rc, "FILEVERSION SVM_VERSION_FILE_VERSION"));
    assert(contains(rc, "PRODUCTVERSION SVM_VERSION_PRODUCT_VERSION"));
    assert(contains(rc, "VALUE \"FileVersion\", SVM_VERSION_FILE_VERSION_STR"));
    assert(contains(rc, "VALUE \"ProductVersion\", SVM_VERSION_PRODUCT_VERSION_STR"));
    assert(!contains(rc, "1,0,0,0"));
    assert(!contains(rc, "\"1.0.0\""));
}

void packageScriptsConsumeVersionSource(const std::filesystem::path& root) {
    const std::string pythonInspector = readText(root / "scripts" / "inspect-windows-package.py");
    const std::string psInspector = readText(root / "scripts" / "inspect-windows-package.ps1");
    const std::string bashPackager = readText(root / "scripts" / "package-windows-native-mingw.sh");
    const std::string psPackager = readText(root / "scripts" / "package-windows-native.ps1");

    assert(contains(pythonInspector, "cmake/svm_version.cmake"));
    assert(contains(pythonInspector, "Native exe file version"));
    assert(contains(pythonInspector, "VERSIONINFO"));
    assert(contains(psInspector, "cmake\\svm_version.cmake"));
    assert(contains(psInspector, "Native exe file version"));
    assert(contains(psInspector, "VERSIONINFO"));
    assert(contains(bashPackager, "SVM_MINGW_PACKAGE_ARTIFACT"));
    assert(contains(psPackager, "SVM_PACKAGE_ARTIFACT"));
}

void docsReferenceCurrentVersion(
    const std::filesystem::path& root,
    const std::map<std::string, std::string>& metadata) {
    const std::string readme = readText(root / "README.md");
    const std::string releaseArtifacts = readText(root / "docs" / "发布产物.md");
    const std::string windowsRelease = readText(root / "docs" / "Windows发布说明.md");
    const std::string docsConsistency = readText(root / "scripts" / "check-docs-artifact-consistency.py");

    assert(contains(readme, "当前版本：" + metadata.at("SVM_RELEASE_TAG")));
    assert(contains(readme, metadata.at("SVM_RELEASE_URL")));
    assert(contains(readme, metadata.at("SVM_PACKAGE_ARTIFACT") + ".zip"));
    assert(contains(releaseArtifacts, "Native exe file version"));
    assert(contains(releaseArtifacts, "Expected version"));
    assert(contains(windowsRelease, "VERSIONINFO"));
    assert(contains(docsConsistency, "cmake/svm_version.cmake"));
}

} // namespace

int main() {
    const std::filesystem::path root = SVM_SOURCE_ROOT;
    const std::filesystem::path buildRoot = SVM_BINARY_DIR;
    const auto metadata = readVersionMetadata(root);

    requiredMetadataKeysExist(metadata);
    versionComponentsMatch(metadata);
    cmakeConsumesVersionSource(root);
    generatedHeaderMatchesSource(buildRoot, metadata);
    win32ResourceUsesGeneratedHeader(root);
    packageScriptsConsumeVersionSource(root);
    docsReferenceCurrentVersion(root, metadata);

    std::cout << "version_metadata_tests passed\n";
    return 0;
}
