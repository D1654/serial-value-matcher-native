#!/usr/bin/env bash
set -euo pipefail

missing=0

require_command() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "缺少命令: $cmd"
    missing=1
  fi
}

require_pkg_config() {
  local module="$1"
  local description="$2"
  if ! pkg-config --exists "$module" 2>/dev/null; then
    echo "未检测到 $description ($module) 开发包"
    missing=1
  fi
}

require_qt_sqlite_driver() {
  local plugin_dir=""
  if command -v qtpaths6 >/dev/null 2>&1; then
    plugin_dir="$(qtpaths6 --plugin-dir 2>/dev/null || true)"
  elif command -v qtpaths >/dev/null 2>&1; then
    plugin_dir="$(qtpaths --plugin-dir 2>/dev/null || true)"
  fi

  local candidates=()
  if [ -n "$plugin_dir" ]; then
    candidates+=("$plugin_dir/sqldrivers/libqsqlite.so")
  fi
  candidates+=(
    "/usr/lib/x86_64-linux-gnu/qt6/plugins/sqldrivers/libqsqlite.so"
    "/usr/lib/qt6/plugins/sqldrivers/libqsqlite.so"
  )

  local found=0
  for candidate in "${candidates[@]}"; do
    if [ -f "$candidate" ]; then
      found=1
      break
    fi
  done

  if [ "$found" -ne 1 ]; then
    echo "未检测到 Qt6 SQLite SQL driver（QSQLITE 插件，Debian 包通常为 libqt6sql6-sqlite）"
    missing=1
  fi
}

require_command cmake
require_command g++
require_command ninja
require_command pkg-config

require_pkg_config Qt6Core "Qt6Core"
require_pkg_config Qt6Widgets "Qt6Widgets"
require_pkg_config Qt6SerialPort "Qt6SerialPort"
require_pkg_config Qt6Sql "Qt6Sql"
require_pkg_config Qt6Test "Qt6Test"
require_qt_sqlite_driver

if [ "$missing" -ne 0 ]; then
  echo "环境尚不能构建 SerialValueMatcher Native。"
  echo "Debian 12 可参考安装：sudo apt install cmake ninja-build pkg-config g++ qt6-base-dev qt6-base-dev-tools qt6-serialport-dev libqt6sql6-sqlite"
  exit 1
fi

echo "环境检查通过：CMake / Ninja / g++ / Qt6 Core Widgets SerialPort Sql Test / QSQLITE 均可用。"
