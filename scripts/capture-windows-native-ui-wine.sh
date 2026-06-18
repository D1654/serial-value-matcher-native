#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

build_dir="${SVM_MINGW_BUILD_DIR:-$repo_root/build-windows-native-mingw}"
exe_path="${SVM_WINE_UI_EXE:-$build_dir/svm-native-win32.exe}"
output_dir="${SVM_WINE_UI_OUTPUT_DIR:-/tmp/svm-native-wine-ui}"
wine_prefix="${SVM_WINEPREFIX:-/tmp/svm-native-wine64-ui}"
xdg_runtime_dir="${SVM_XDG_RUNTIME_DIR:-/tmp/svm-native-xdg-runtime}"
window_name="${SVM_WINE_UI_WINDOW_NAME:-串口值匹配器}"
screen_spec="${SVM_WINE_UI_SCREEN:-1280x900x24}"
stabilize_seconds="${SVM_WINE_UI_STABILIZE_SECONDS:-8}"
capture_tabs="${SVM_WINE_UI_CAPTURE_TABS:-1}"
capture_compact="${SVM_WINE_UI_CAPTURE_COMPACT:-1}"
capture_fast_frames="${SVM_WINE_UI_CAPTURE_FAST_FRAMES:-1}"
fast_frame_delay="${SVM_WINE_UI_FAST_FRAME_DELAY:-0.15}"

require_command() {
    local name="$1"
    if ! command -v "$name" >/dev/null 2>&1; then
        echo "缺少命令：$name" >&2
        exit 2
    fi
}

require_command wine
require_command winepath
require_command wineserver
require_command xvfb-run
require_command xdotool
require_command xwd
require_command convert

if [[ ! -f "$exe_path" ]]; then
    echo "未找到 svm-native-win32.exe：$exe_path" >&2
    exit 3
fi

mkdir -p "$output_dir" "$wine_prefix" "$xdg_runtime_dir"
chmod 700 "$xdg_runtime_dir"
rm -f \
    "$output_dir"/root.png \
    "$output_dir"/tab-*.png \
    "$output_dir"/compact-tab-*.png \
    "$output_dir"/self-test.log \
    "$output_dir"/app.stdout \
    "$output_dir"/app.stderr \
    "$output_dir"/window-info.txt \
    "$output_dir"/window.ids \
    "$output_dir"/tab-clicks.txt \
    "$output_dir"/xdotool.err

export XDG_RUNTIME_DIR="$xdg_runtime_dir"
export WINEPREFIX="$wine_prefix"
export WINEARCH=win64

wineboot -u >"$output_dir/wineboot.log" 2>&1
{
    for font_name in \
        'Microsoft YaHei UI' \
        'Microsoft YaHei' \
        'MS Shell Dlg' \
        'MS Shell Dlg 2' \
        'Microsoft Sans Serif' \
        'Tahoma' \
        'SimSun'
    do
        wine reg add 'HKCU\Software\Wine\Fonts\Replacements' /v "$font_name" /d 'Noto Sans CJK SC' /f
    done
} >"$output_dir/font-replacement.log" 2>&1 || true

self_test_log_windows="$(winepath -w "$output_dir/self-test.log" | tr -d '\r')"
SVM_NATIVE_SELF_TEST_LOG="$self_test_log_windows" wine "$exe_path" --self-test

xvfb-run -a -s "-screen 0 $screen_spec" bash -c '
set -euo pipefail

exe_path="$1"
output_dir="$2"
window_name="$3"
stabilize_seconds="$4"
capture_tabs="$5"
capture_compact="$6"
capture_fast_frames="$7"
fast_frame_delay="$8"

wine "$exe_path" >"$output_dir/app.stdout" 2>"$output_dir/app.stderr" &
app_pid=$!

cleanup() {
    kill "$app_pid" >/dev/null 2>&1 || true
    wineserver -k >/dev/null 2>&1 || true
}
trap cleanup EXIT

found=0
for _ in $(seq 1 30); do
    if xdotool search --onlyvisible --name "$window_name" >"$output_dir/window.ids" 2>"$output_dir/xdotool.err"; then
        found=1
        break
    fi
    sleep 0.5
done

if [[ "$found" -ne 1 ]]; then
    echo "未找到目标窗口：$window_name" >&2
    exit 4
fi

sleep "$stabilize_seconds"
xwd -root -silent | convert xwd:- "$output_dir/root.png"

while read -r window_id; do
    xdotool getwindowname "$window_id"
    xdotool getwindowgeometry "$window_id"
done <"$output_dir/window.ids" >"$output_dir/window-info.txt" 2>&1

capture_screen() {
    local file="$1"
    xwd -root -silent | convert xwd:- "$file"
}

assert_screenshot_visible() {
    local file="$1"
    if [[ ! -s "$file" ]]; then
        echo "截图为空：$file" >&2
        exit 6
    fi
    local mean
    mean="$(convert "$file" -colorspace Gray -format "%[fx:mean]" info: 2>/dev/null || true)"
    if [[ -z "$mean" ]]; then
        echo "无法分析截图亮度：$file" >&2
        exit 6
    fi
    if awk -v mean="$mean" "BEGIN { exit !(mean > 0.985) }"; then
        echo "截图疑似全白/空白：$file mean=$mean" >&2
        exit 6
    fi
}

capture_checked_screen() {
    local file="$1"
    capture_screen "$file"
    assert_screenshot_visible "$file"
}

capture_tab_set() {
    local window_id="$1"
    local prefix="$2"
    local fast_frames="$3"
    local fast_delay="$4"
    local geometry
    geometry="$(xdotool getwindowgeometry --shell "$window_id")"
    eval "$geometry"
    local tab_y_offset="${SVM_WINE_UI_TAB_Y_OFFSET:-205}"
    if [[ "$prefix" == compact-tab-* ]]; then
        tab_y_offset="${SVM_WINE_UI_COMPACT_TAB_Y_OFFSET:-205}"
    fi
    local tab_y=$((HEIGHT - tab_y_offset))
    if [[ "$tab_y" -lt 80 ]]; then
        tab_y=$((HEIGHT - 120))
    fi
    printf "%s X=%s Y=%s WIDTH=%s HEIGHT=%s tab_y_offset=%s tab_y=%s\n" "$prefix" "$X" "$Y" "$WIDTH" "$HEIGHT" "$tab_y_offset" "$tab_y" >> "$output_dir/tab-clicks.txt"
    local tab_names=(single quick file scan settings)
    local tab_widths=(46 54 54 54 54)
    local tab_left="${SVM_WINE_UI_TAB_LEFT:-14}"
    local cursor="$tab_left"
    for index in "${!tab_names[@]}"; do
        local tab_x=$((cursor + tab_widths[index] / 2))
        cursor=$((cursor + tab_widths[index]))
        xdotool windowactivate --sync "$window_id" >/dev/null 2>&1 || true
        xdotool mousemove --sync "$((X + tab_x))" "$((Y + tab_y))" click 1
        if [[ "$fast_frames" != "0" ]]; then
            sleep "$fast_delay"
            capture_checked_screen "$output_dir/${prefix}${tab_names[$index]}-fast.png"
        fi
        sleep 0.55
        capture_checked_screen "$output_dir/${prefix}${tab_names[$index]}.png"
    done
}

if [[ "$capture_tabs" != "0" ]]; then
    first_window_id="$(head -n 1 "$output_dir/window.ids")"
    capture_tab_set "$first_window_id" "tab-" "$capture_fast_frames" "$fast_frame_delay"
    if [[ "$capture_compact" != "0" ]]; then
        xdotool windowsize --sync "$first_window_id" 760 520 >/dev/null 2>&1 || true
        sleep 1
        capture_tab_set "$first_window_id" "compact-tab-" "$capture_fast_frames" "$fast_frame_delay"
    fi
fi
' bash "$exe_path" "$output_dir" "$window_name" "$stabilize_seconds" "$capture_tabs" "$capture_compact" "$capture_fast_frames" "$fast_frame_delay"

echo "Wine UI 截图完成：$output_dir/root.png"
echo "窗口信息：$output_dir/window-info.txt"
echo "self-test 日志：$output_dir/self-test.log"
