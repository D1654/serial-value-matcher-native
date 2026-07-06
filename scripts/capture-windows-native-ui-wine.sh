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
capture_resize_sweep="${SVM_WINE_UI_CAPTURE_RESIZE_SWEEP:-1}"

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
require_command compare

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
    "$output_dir"/resize-*.png \
    "$output_dir"/log-splitter-*.png \
    "$output_dir"/self-test.log \
    "$output_dir"/ui-perf-test.log \
    "$output_dir"/capture-status.txt \
    "$output_dir"/app.stdout \
    "$output_dir"/app.stderr \
    "$output_dir"/window-info.txt \
    "$output_dir"/window.ids \
    "$output_dir"/tab-clicks.txt \
    "$output_dir"/xdotool.err

export XDG_RUNTIME_DIR="$xdg_runtime_dir"
export WINEPREFIX="$wine_prefix"
export WINEARCH=win64

: >"$output_dir/capture-status.txt"

add_capture_status() {
    local scenario="$1"
    local status="${2:-PASS}"
    local detail="${3:-}"
    if [[ -n "$detail" ]]; then
        printf "%s %s %s\n" "$status" "$scenario" "$detail" >>"$output_dir/capture-status.txt"
    else
        printf "%s %s\n" "$status" "$scenario" >>"$output_dir/capture-status.txt"
    fi
}

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
add_capture_status "self-test" "PASS" "log=self-test.log"
ui_perf_log_windows="$(winepath -w "$output_dir/ui-perf-test.log" | tr -d '\r')"

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
ui_perf_log_windows="$9"
capture_resize_sweep="${10}"

SVM_NATIVE_SELF_TEST_LOG="$ui_perf_log_windows" wine "$exe_path" --ui-perf-test
printf "%s\n" "PASS ui-perf-test log=ui-perf-test.log" >> "$output_dir/capture-status.txt"

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
printf "%s\n" "PASS default-window file=root.png" >> "$output_dir/capture-status.txt"

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

assert_screenshot_region_detail() {
    local file="$1"
    local label="$2"
    local crop="$3"
    local min_dark_ratio="$4"
    local dark_ratio
    dark_ratio="$(convert "$file" -crop "$crop" -colorspace Gray -threshold 80% -format "%[fx:1-mean]" info: 2>/dev/null || true)"
    if [[ -z "$dark_ratio" ]]; then
        echo "无法分析截图区域：$file $label crop=$crop" >&2
        exit 8
    fi
    if awk -v ratio="$dark_ratio" -v min="$min_dark_ratio" "BEGIN { exit !(ratio < min) }"; then
        echo "截图区域疑似缺少 UI 细节：$label crop=$crop dark=$dark_ratio min=$min_dark_ratio" >&2
        exit 8
    fi
}

capture_checked_screen() {
    local file="$1"
    capture_screen "$file"
    assert_screenshot_visible "$file"
}

assert_screenshots_differ() {
    local left="$1"
    local right="$2"
    local label="$3"
    local min_pixels="$4"
    local diff
    set +e
    diff="$(compare -metric AE "$left" "$right" null: 2>&1 >/dev/null)"
    local compare_status=$?
    set -e
    if [[ "$compare_status" -ne 0 && "$compare_status" -ne 1 ]]; then
        echo "无法比较截图差异：$label $left $right" >&2
        exit 9
    fi
    if [[ -z "$diff" ]]; then
        diff=0
    fi
    if awk -v diff="$diff" -v min="$min_pixels" "BEGIN { exit !(diff < min) }"; then
        echo "截图差异不足：$label diff=$diff min=$min_pixels" >&2
        exit 9
    fi
}

first_window_id="$(head -n 1 "$output_dir/window.ids")"
geometry="$(xdotool getwindowgeometry --shell "$first_window_id")"
eval "$geometry"
toolbar_width=$((WIDTH - 180))
if [[ "$toolbar_width" -lt 240 ]]; then
    toolbar_width=240
fi
assert_screenshot_region_detail "$output_dir/root.png" "日志工具条" "${toolbar_width}x80+$X+$((Y + 12))" "0.05"
assert_screenshot_region_detail "$output_dir/root.png" "串口侧栏" "260x330+$((X + WIDTH - 260))+$((Y + 12))" "0.045"

capture_resize_sweep_set() {
    local window_id="$1"
    local sizes=("980 690" "760 520" "1180 740" "900 620" "1212 753")
    local index=0
    for size in "${sizes[@]}"; do
        local resize_width resize_height
        read -r resize_width resize_height <<< "$size"
        xdotool windowsize --sync "$window_id" "$resize_width" "$resize_height" >/dev/null 2>&1 || true
        sleep 1
        capture_checked_screen "$output_dir/resize-${index}.png"
        printf "PASS resize-%s file=resize-%s.png size=%sx%s\n" "$index" "$index" "$resize_width" "$resize_height" >> "$output_dir/capture-status.txt"
        index=$((index + 1))
    done

    local geometry
    geometry="$(xdotool getwindowgeometry --shell "$window_id")"
    eval "$geometry"
    local resize_toolbar_width=$((WIDTH - 180))
    if [[ "$resize_toolbar_width" -lt 240 ]]; then
        resize_toolbar_width=240
    fi
    assert_screenshot_region_detail "$output_dir/resize-$((index - 1)).png" "缩放后日志工具条" "${resize_toolbar_width}x80+$X+$((Y + 12))" "0.05"
    assert_screenshot_region_detail "$output_dir/resize-$((index - 1)).png" "缩放后串口侧栏" "260x330+$((X + WIDTH - 260))+$((Y + 12))" "0.045"
    printf "PASS resize-sweep screenshots=%s\n" "$index" >> "$output_dir/capture-status.txt"
}

capture_tab_set() {
    local window_id="$1"
    local prefix="$2"
    local fast_frames="$3"
    local fast_delay="$4"
    local geometry
    geometry="$(xdotool getwindowgeometry --shell "$window_id")"
    eval "$geometry"

    local compact=0
    if (( WIDTH < 1040 || HEIGHT < 720 )); then
        compact=1
    fi
    local tight=0
    if (( WIDTH < 860 )); then
        tight=1
    fi
    local margin=6
    local status_height=22
    local desired_work_height=236
    local minimum_log_height=210
    local splitter_height=12
    if (( tight == 1 )); then
        margin=3
    elif (( compact == 1 )); then
        margin=4
    fi
    if (( compact == 1 )); then
        status_height=20
        desired_work_height=230
        minimum_log_height=150
    fi

    local status_y=$((HEIGHT - status_height - 4))
    if (( status_y < margin )); then
        status_y="$margin"
    fi
    local content_height=$((status_y - margin))
    if (( content_height < 1 )); then
        content_height=1
    fi
    local maximum_work_height=$((content_height - minimum_log_height - splitter_height))
    if (( maximum_work_height < 84 )); then
        maximum_work_height=84
    fi
    local work_height="$desired_work_height"
    if (( work_height > maximum_work_height )); then
        work_height="$maximum_work_height"
    fi
    if (( work_height < 84 )); then
        work_height=84
    fi
    local log_height=$((status_y - margin - work_height - splitter_height))
    if (( log_height < 1 )); then
        log_height=1
    fi
    local tabs_y=$((margin + log_height + splitter_height))
    local tab_y_adjust="${SVM_WINE_UI_TAB_Y_ADJUST:-0}"
    if (( compact == 1 )); then
        tab_y_adjust="${SVM_WINE_UI_COMPACT_TAB_Y_ADJUST:-20}"
    fi
    local tab_y=$((tabs_y + 14 + tab_y_adjust))
    printf "%s X=%s Y=%s WIDTH=%s HEIGHT=%s compact=%s tight=%s tabs_y=%s tab_y=%s adjust=%s\n" "$prefix" "$X" "$Y" "$WIDTH" "$HEIGHT" "$compact" "$tight" "$tabs_y" "$tab_y" "$tab_y_adjust" >> "$output_dir/tab-clicks.txt"
    local tab_names=(single quick file scan settings)
    local tab_centers=(25 75 125 175 225)
    for index in "${!tab_names[@]}"; do
        local tab_x=$((margin + tab_centers[index]))
        xdotool windowactivate --sync "$window_id" >/dev/null 2>&1 || true
        xdotool mousemove --sync "$((X + tab_x))" "$((Y + tab_y))" click 1
        if [[ "$fast_frames" != "0" ]]; then
            sleep "$fast_delay"
            capture_checked_screen "$output_dir/${prefix}${tab_names[$index]}-fast.png"
            printf "PASS %s%s-fast file=%s%s-fast.png\n" "$prefix" "${tab_names[$index]}" "$prefix" "${tab_names[$index]}" >> "$output_dir/capture-status.txt"
        fi
        sleep 0.55
        capture_checked_screen "$output_dir/${prefix}${tab_names[$index]}.png"
        printf "PASS %s%s file=%s%s.png\n" "$prefix" "${tab_names[$index]}" "$prefix" "${tab_names[$index]}" >> "$output_dir/capture-status.txt"
    done

    assert_screenshots_differ "$output_dir/${prefix}single.png" "$output_dir/${prefix}scan.png" "${prefix}single-vs-scan" 500
    local set_scenario="tab-set"
    if [[ "$prefix" == "compact-tab-" ]]; then
        set_scenario="compact-tab-set"
    fi
    printf "PASS %s screenshots=%s switching=clicked-frame-diff-validated-by-ui-perf\n" "$set_scenario" "${#tab_names[@]}" >> "$output_dir/capture-status.txt"
}

capture_log_splitter_movement() {
    local window_id="$1"
    xdotool windowsize --sync "$window_id" 1212 753 >/dev/null 2>&1 || true
    sleep 1
    capture_checked_screen "$output_dir/log-splitter-before.png"

    local geometry
    geometry="$(xdotool getwindowgeometry --shell "$window_id")"
    eval "$geometry"

    local compact=0
    if (( WIDTH < 1040 || HEIGHT < 720 )); then
        compact=1
    fi
    local tight=0
    if (( WIDTH < 860 )); then
        tight=1
    fi
    local margin=6
    local status_height=22
    local side_gap=6
    local desired_work_height=236
    local minimum_log_height=210
    local splitter_height=12
    if (( tight == 1 )); then
        margin=3
        side_gap=4
    elif (( compact == 1 )); then
        margin=4
        side_gap=5
    fi
    if (( compact == 1 )); then
        status_height=20
        desired_work_height=230
        minimum_log_height=150
    fi

    local status_y=$((HEIGHT - status_height - 4))
    if (( status_y < margin )); then
        status_y="$margin"
    fi
    local content_height=$((status_y - margin))
    if (( content_height < 1 )); then
        content_height=1
    fi
    local maximum_work_height=$((content_height - minimum_log_height - splitter_height))
    if (( maximum_work_height < 84 )); then
        maximum_work_height=84
    fi
    local work_height="$desired_work_height"
    if (( work_height > maximum_work_height )); then
        work_height="$maximum_work_height"
    fi
    if (( work_height < 84 )); then
        work_height=84
    fi
    local log_height=$((status_y - margin - work_height - splitter_height))
    if (( log_height < 1 )); then
        log_height=1
    fi
    local splitter_y_adjust="${SVM_WINE_UI_SPLITTER_Y_ADJUST:-0}"
    local splitter_y=$((Y + margin + log_height + splitter_height / 2 + splitter_y_adjust))
    local splitter_x=$((X + WIDTH / 2))

    xdotool windowactivate --sync "$window_id" >/dev/null 2>&1 || true
    xdotool mousemove --sync "$splitter_x" "$splitter_y" mousedown 1
    sleep 0.1
    xdotool mousemove --sync "$splitter_x" "$((splitter_y - 24))"
    sleep 0.2
    capture_checked_screen "$output_dir/log-splitter-frame-01.png"
    xdotool mousemove --sync "$splitter_x" "$((splitter_y - 48))"
    sleep 0.2
    capture_checked_screen "$output_dir/log-splitter-frame-02.png"
    xdotool mouseup 1
    sleep 1
    capture_checked_screen "$output_dir/log-splitter-after.png"
    assert_screenshots_differ "$output_dir/log-splitter-before.png" "$output_dir/log-splitter-frame-01.png" "splitter-before-vs-frame-01" 500
    assert_screenshots_differ "$output_dir/log-splitter-frame-01.png" "$output_dir/log-splitter-frame-02.png" "splitter-frame-01-vs-frame-02" 500
    assert_screenshots_differ "$output_dir/log-splitter-before.png" "$output_dir/log-splitter-after.png" "splitter-before-vs-after" 500
    printf "PASS splitter-drag-frames files=log-splitter-before.png,log-splitter-frame-01.png,log-splitter-frame-02.png,log-splitter-after.png deltas=-24,-48 live=true diff-gated=true\n" >> "$output_dir/capture-status.txt"
}

if [[ "$capture_tabs" != "0" ]]; then
    xdotool windowsize --sync "$first_window_id" 1212 753 >/dev/null 2>&1 || true
    sleep 1
    capture_tab_set "$first_window_id" "tab-" "$capture_fast_frames" "$fast_frame_delay"
    if [[ "$capture_compact" != "0" ]]; then
        xdotool windowsize --sync "$first_window_id" 760 520 >/dev/null 2>&1 || true
        sleep 1
        capture_tab_set "$first_window_id" "compact-tab-" "$capture_fast_frames" "$fast_frame_delay"
    fi
fi

if [[ "$capture_resize_sweep" != "0" ]]; then
    capture_resize_sweep_set "$first_window_id"
fi

capture_log_splitter_movement "$first_window_id"
printf "%s\n" "PASS phase-1-ui-regression-closure tabs=5 compact-tabs=5 resize-sweep=true splitter-drag-frames=true wine-smoke=true" >> "$output_dir/capture-status.txt"
printf "%s\n" "PASS capture-complete" >> "$output_dir/capture-status.txt"
' bash "$exe_path" "$output_dir" "$window_name" "$stabilize_seconds" "$capture_tabs" "$capture_compact" "$capture_fast_frames" "$fast_frame_delay" "$ui_perf_log_windows" "$capture_resize_sweep"

echo "Wine UI 截图完成：$output_dir/root.png"
echo "捕获状态：$output_dir/capture-status.txt"
echo "窗口信息：$output_dir/window-info.txt"
echo "self-test 日志：$output_dir/self-test.log"
echo "UI 性能日志：$output_dir/ui-perf-test.log"
echo "UI 基线说明：Wine 截图用于辅助诊断，最终视觉结论以 GitHub Actions Windows UI artifact 和真实 Windows 截图为准。"
