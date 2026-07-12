#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

build_dir="${SVM_MINGW_BUILD_DIR:-$repo_root/build-windows-native-mingw}"
build_type="${SVM_MINGW_BUILD_TYPE:-Release}"
parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-1}"

require_command() {
    local name="$1"
    if ! command -v "$name" >/dev/null 2>&1; then
        echo "缺少命令：$name" >&2
        exit 2
    fi
}

require_command cmake
require_command ninja
require_command x86_64-w64-mingw32-g++
require_command x86_64-w64-mingw32-windres

cmake -S "$repo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/toolchains/mingw64.cmake" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DSVM_BUILD_WIN32_APP=ON

cmake --build "$build_dir" --target svm-native-win32 --parallel "$parallel"

exe_path="$build_dir/svm-native-win32.exe"
if [[ ! -f "$exe_path" ]]; then
    echo "构建完成但未找到：$exe_path" >&2
    exit 3
fi

file "$exe_path"
echo "Windows native MinGW 构建完成：$exe_path"
