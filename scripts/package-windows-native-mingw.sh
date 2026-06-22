#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

build_dir="${SVM_MINGW_BUILD_DIR:-$repo_root/build-windows-native-mingw}"
package_root="${SVM_MINGW_PACKAGE_DIR:-$repo_root/artifacts/windows-native-mingw}"
package_name="${SVM_MINGW_PACKAGE_NAME:-SerialValueMatcherNative-win32-native-x64-mingw}"
max_zip_bytes="${SVM_NATIVE_MAX_ZIP_BYTES:-5242880}"
max_extracted_bytes="${SVM_NATIVE_MAX_EXTRACTED_BYTES:-8388608}"
skip_build=0
skip_wine="${SVM_SKIP_WINE_TEST:-0}"
strict_wine="${SVM_STRICT_WINE_TEST:-0}"
wine_prefix="${SVM_WINEPREFIX:-/tmp/svm-native-wine64}"
xdg_runtime_dir="${SVM_XDG_RUNTIME_DIR:-/tmp/svm-native-xdg-runtime}"

for arg in "$@"; do
    case "$arg" in
        --skip-build)
            skip_build=1
            ;;
        --skip-wine)
            skip_wine=1
            ;;
        *)
            echo "未知参数：$arg" >&2
            exit 2
            ;;
    esac
done

require_command() {
    local name="$1"
    if ! command -v "$name" >/dev/null 2>&1; then
        echo "缺少命令：$name" >&2
        exit 2
    fi
}

if [[ "$skip_build" -eq 0 ]]; then
    "$repo_root/scripts/build-windows-native-mingw.sh"
fi

require_command python3
require_command 7z
require_command x86_64-w64-mingw32-strip

exe_path="$build_dir/svm-native-win32.exe"
if [[ ! -f "$exe_path" ]]; then
    echo "未找到 svm-native-win32.exe：$exe_path" >&2
    exit 3
fi

if [[ "$skip_wine" != "1" ]]; then
    if command -v wine >/dev/null 2>&1 && command -v xvfb-run >/dev/null 2>&1; then
        echo "运行 Wine/Xvfb native self-test 和 UI 性能门禁..."
        mkdir -p "$wine_prefix" "$xdg_runtime_dir"
        chmod 700 "$xdg_runtime_dir"
        if ! env WINEPREFIX="$wine_prefix" WINEARCH=win64 XDG_RUNTIME_DIR="$xdg_runtime_dir" \
            xvfb-run -a bash -c 'wine "$1" --self-test && wine "$1" --ui-perf-test' bash "$exe_path"; then
            if [[ "$strict_wine" == "1" ]]; then
                echo "Wine/Xvfb native self-test 或 UI 性能门禁失败。" >&2
                exit 4
            fi
            echo "警告：Wine/Xvfb native self-test 或 UI 性能门禁失败，继续执行静态打包检查。设置 SVM_STRICT_WINE_TEST=1 可将其作为硬门禁。" >&2
        fi
    else
        echo "跳过 Wine/Xvfb self-test 和 UI 性能门禁：wine 或 xvfb-run 不可用。"
    fi
fi

stage_dir="$package_root/$package_name"
zip_path="$package_root/$package_name.zip"
hash_path="$zip_path.sha256.txt"
summary_path="$package_root/$package_name.package-summary.txt"

rm -rf "$stage_dir"
mkdir -p "$stage_dir/docs"
cp "$exe_path" "$stage_dir/"
x86_64-w64-mingw32-strip --strip-all "$stage_dir/svm-native-win32.exe"
cp "$repo_root/README.md" "$stage_dir/"

for relative_doc in \
    docs/windows-native-slimming.md \
    docs/windows-native-ui-validation.md \
    docs/windows-serial-validation.md \
    docs/windows-deployment.md \
    docs/windows-native-local-debug.md
do
    if [[ -f "$repo_root/$relative_doc" ]]; then
        cp "$repo_root/$relative_doc" "$stage_dir/docs/"
    fi
done

rm -f "$zip_path" "$hash_path" "$summary_path"
mkdir -p "$package_root"
(
    cd "$stage_dir"
    7z a -tzip "$zip_path" ./*
)
sha256sum "$zip_path" > "$hash_path"

python3 "$repo_root/scripts/inspect-windows-package.py" \
    --stage-dir "$stage_dir" \
    --zip-path "$zip_path" \
    --summary-path "$summary_path" \
    --max-zip-bytes "$max_zip_bytes" \
    --max-extracted-bytes "$max_extracted_bytes"

echo "Windows native MinGW 本地包完成：$zip_path"
echo "SHA256：$(cut -d ' ' -f 1 "$hash_path")"
echo "体积摘要：$summary_path"
