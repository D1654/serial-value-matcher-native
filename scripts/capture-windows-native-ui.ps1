param(
    [string]$BuildDir = "build-windows-native",
    [string]$Config = "Release",
    [string]$ExePath = "",
    [string]$OutputDir = "artifacts\windows-native-ui",
    [string]$WindowTitle = "串口值匹配器 Win32 Native",
    [int]$DefaultWidth = 1212,
    [int]$DefaultHeight = 753,
    [int]$CompactWidth = 760,
    [int]$CompactHeight = 520,
    [switch]$SkipUiPerfTest
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

$repoRoot = Resolve-RepoRoot
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$captureStatusPath = Join-Path $outputPath "capture-status.txt"
$selfTestLog = Join-Path $outputPath "self-test.log"
$uiPerfLog = Join-Path $outputPath "ui-perf-test.log"
$uiEvidenceSummaryPath = Join-Path $outputPath "ui-evidence-summary.txt"

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    $exeCandidates = @(
        (Join-Path $buildPath "$Config\svm-native-win32.exe"),
        (Join-Path $buildPath "svm-native-win32.exe")
    )
    $ExePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $ExePath -or -not (Test-Path $ExePath)) {
    throw "未找到 svm-native-win32.exe。BuildDir=$BuildDir Config=$Config"
}

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
if (Test-Path $outputPath) {
    $preservedLogNames = @("self-test.log")
    if ($SkipUiPerfTest) {
        $preservedLogNames += "ui-perf-test.log"
    }
    Get-ChildItem -Path $outputPath -File | Where-Object {
        $preservedLogNames -notcontains $_.Name
    } | Remove-Item -Force
}
New-Item -ItemType File -Path $captureStatusPath -Force | Out-Null

function Add-CaptureStatus {
    param(
        [string]$Scenario,
        [string]$Status = "PASS",
        [string]$Detail = ""
    )

    $line = if ([string]::IsNullOrWhiteSpace($Detail)) {
        "$Status $Scenario"
    } else {
        "$Status $Scenario $Detail"
    }
    Add-Content -Path $captureStatusPath -Value $line -Encoding UTF8
}

function Write-UiEvidenceSummary {
    $statusLines = if (Test-Path $captureStatusPath) {
        @(Get-Content -Path $captureStatusPath -Encoding UTF8)
    } else {
        @()
    }
    $requiredStatusTerms = @(
        "PASS default-window",
        "PASS tab-set",
        "PASS compact-tab-set",
        "PASS resize-sweep",
        "PASS dpi-smoke-100",
        "PASS dpi-smoke-125",
        "PASS splitter-drag-frames",
        "PASS phase-1-ui-regression-closure",
        "PASS capture-complete"
    )
    $missingStatusTerms = @()
    $statusText = $statusLines -join "`n"
    foreach ($term in $requiredStatusTerms) {
        if (-not $statusText.Contains($term)) {
            $missingStatusTerms += $term
        }
    }
    $pngFiles = @(Get-ChildItem -Path $outputPath -Filter "*.png" -File -ErrorAction SilentlyContinue)
    $selfTestText = if (Test-Path $selfTestLog) { Get-Content -Path $selfTestLog -Raw -Encoding UTF8 } else { "" }
    $uiPerfText = if (Test-Path $uiPerfLog) { Get-Content -Path $uiPerfLog -Raw -Encoding UTF8 } else { "" }
    $hasFailStatus = $statusLines | Where-Object { $_.StartsWith("FAIL ") } | Select-Object -First 1
    $selfTestStatus = if ($selfTestText.Contains("ok")) { "passed" } else { "missing-or-failed" }
    $uiPerfStatus = if ($uiPerfText.Contains("ui-perf ok")) { "passed" } else { "missing-or-failed" }
    $gatePassed = $missingStatusTerms.Count -eq 0 -and
        $null -eq $hasFailStatus -and
        $pngFiles.Count -ge 18 -and
        $selfTestStatus -eq "passed" -and
        $uiPerfStatus -eq "passed" -and
        (Test-Path (Join-Path $outputPath "window-info.txt"))

    $summaryLines = @(
        "UI evidence summary",
        "GeneratedAt=$((Get-Date).ToString("o"))",
        "Artifact=windows-native-ui-screenshots",
        "BaselinePolicy=release-artifact-derived",
        "PngCount=$($pngFiles.Count)",
        "CaptureStatusFile=capture-status.txt",
        "MissingStatusTerms=$(if ($missingStatusTerms.Count -eq 0) { 'none' } else { $missingStatusTerms -join ', ' })",
        "HasFailStatus=$(if ($null -eq $hasFailStatus) { 'no' } else { 'yes' })",
        "SelfTestStatus=$selfTestStatus",
        "UiPerfStatus=$uiPerfStatus",
        "WindowInfoPresent=$(if (Test-Path (Join-Path $outputPath "window-info.txt")) { 'yes' } else { 'no' })",
        "RequiredScenarios=default-window,tab-set,compact-tab-set,resize-sweep,dpi-smoke-100,dpi-smoke-125,splitter-drag-frames,phase-1-ui-regression-closure,capture-complete",
        "GateStatus=$(if ($gatePassed) { 'passed' } else { 'failed' })"
    )
    $summaryLines | Set-Content -Path $uiEvidenceSummaryPath -Encoding UTF8
}

if (-not (Test-Path $selfTestLog) -or (Get-Item $selfTestLog).Length -le 0) {
    $previousSelfTestLog = [Environment]::GetEnvironmentVariable("SVM_NATIVE_SELF_TEST_LOG", "Process")
    [Environment]::SetEnvironmentVariable("SVM_NATIVE_SELF_TEST_LOG", $selfTestLog, "Process")
    try {
        Write-Host "运行 native self-test..."
        $selfTest = Start-Process -FilePath $ExePath -ArgumentList "--self-test" -Wait -PassThru
        if ($selfTest.ExitCode -ne 0) {
            throw "native self-test 失败，退出码：$($selfTest.ExitCode)"
        }
        Add-CaptureStatus -Scenario "self-test" -Detail "log=self-test.log"
    } finally {
        [Environment]::SetEnvironmentVariable("SVM_NATIVE_SELF_TEST_LOG", $previousSelfTestLog, "Process")
    }
} else {
    Add-CaptureStatus -Scenario "self-test" -Detail "preexisting-log=self-test.log"
}

if (-not $SkipUiPerfTest) {
    $previousSelfTestLog = [Environment]::GetEnvironmentVariable("SVM_NATIVE_SELF_TEST_LOG", "Process")
    [Environment]::SetEnvironmentVariable("SVM_NATIVE_SELF_TEST_LOG", $uiPerfLog, "Process")
    try {
        Write-Host "运行 UI 性能门禁..."
        $uiPerfTest = Start-Process -FilePath $ExePath -ArgumentList "--ui-perf-test" -Wait -PassThru
        if ($uiPerfTest.ExitCode -ne 0) {
            throw "UI 性能门禁失败，退出码：$($uiPerfTest.ExitCode)"
        }
        Add-CaptureStatus -Scenario "ui-perf-test" -Detail "log=ui-perf-test.log"
    } finally {
        [Environment]::SetEnvironmentVariable("SVM_NATIVE_SELF_TEST_LOG", $previousSelfTestLog, "Process")
    }
} else {
    Write-Host "跳过 UI 性能门禁：调用方已完成独立验证。"
    if (-not (Test-Path $uiPerfLog)) {
        Add-CaptureStatus -Scenario "ui-perf-test" -Status "FAIL" -Detail "missing-preexisting-log"
        throw "SkipUiPerfTest 要求调用方预先生成 ui-perf-test.log。"
    }
    Add-CaptureStatus -Scenario "ui-perf-test" -Detail "preexisting-log=ui-perf-test.log"
}

try {
    Add-Type -AssemblyName System.Drawing.Common -ErrorAction Stop
} catch {
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
}
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class NativeUiCapture {
    public const int SW_RESTORE = 9;
    public const uint SWP_NOZORDER = 0x0004;
    public const uint SWP_SHOWWINDOW = 0x0040;
    public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    public const uint MOUSEEVENTF_LEFTUP = 0x0004;
    public const int LOGPIXELSX = 88;
    public const int LOGPIXELSY = 90;

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    public static extern int GetDeviceCaps(IntPtr hdc, int nIndex);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
}
"@

function Set-CaptureWindowSize {
    param(
        [IntPtr]$WindowHandle,
        [int]$Width,
        [int]$Height,
        [int]$DelayMilliseconds = 800
    )

    [NativeUiCapture]::ShowWindow($WindowHandle, [NativeUiCapture]::SW_RESTORE) | Out-Null
    $setWindowFlags = [NativeUiCapture]::SWP_NOZORDER -bor [NativeUiCapture]::SWP_SHOWWINDOW
    if (-not [NativeUiCapture]::SetWindowPos(
            $WindowHandle,
            [IntPtr]::Zero,
            0,
            0,
            $Width,
            $Height,
            $setWindowFlags)) {
        throw "设置窗口尺寸失败：${Width}x${Height}"
    }
    [NativeUiCapture]::SetForegroundWindow($WindowHandle) | Out-Null
    Start-Sleep -Milliseconds $DelayMilliseconds
}

function Get-NativeWindowSize {
    param([IntPtr]$WindowHandle)

    $rect = New-Object NativeUiCapture+RECT
    if (-not [NativeUiCapture]::GetWindowRect($WindowHandle, [ref]$rect)) {
        throw "GetWindowRect 失败。"
    }

    return [PSCustomObject]@{
        Width = [Math]::Max(1, $rect.Right - $rect.Left)
        Height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    }
}

function Capture-WindowImage {
    param(
        [IntPtr]$WindowHandle,
        [string]$Path
    )

    $rect = New-Object NativeUiCapture+RECT
    if (-not [NativeUiCapture]::GetWindowRect($WindowHandle, [ref]$rect)) {
        throw "GetWindowRect 失败。"
    }

    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bitmap = New-Object System.Drawing.Bitmap -ArgumentList @($width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $hdc = $graphics.GetHdc()
        $printed = $false
        try {
            $printed = [NativeUiCapture]::PrintWindow($WindowHandle, $hdc, 2)
        } finally {
            $graphics.ReleaseHdc($hdc)
        }

        if (-not $printed) {
            $graphics.CopyFromScreen(
                $rect.Left,
                $rect.Top,
                0,
                0,
                (New-Object System.Drawing.Size -ArgumentList @($width, $height)))
        }

        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Assert-ImageVisible {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "截图未生成：$Path"
    }
    if ((Get-Item $Path).Length -lt 2048) {
        throw "截图过小，疑似失败：$Path"
    }

    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $stepX = [Math]::Max(1, [int]($bitmap.Width / 48))
        $stepY = [Math]::Max(1, [int]($bitmap.Height / 48))
        $min = 255
        $max = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += $stepY) {
            for ($x = 0; $x -lt $bitmap.Width; $x += $stepX) {
                $pixel = $bitmap.GetPixel($x, $y)
                $brightness = [int](($pixel.R + $pixel.G + $pixel.B) / 3)
                if ($brightness -lt $min) { $min = $brightness }
                if ($brightness -gt $max) { $max = $brightness }
            }
        }
        if (($max - $min) -lt 10) {
            throw "截图对比度过低，疑似空白：$Path"
        }
    } finally {
        $bitmap.Dispose()
    }
}

function Get-ImageDarkRatio {
    param(
        [string]$Path,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height
    )

    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $left = [Math]::Max(0, $X)
        $top = [Math]::Max(0, $Y)
        $right = [Math]::Min($bitmap.Width, $left + [Math]::Max(1, $Width))
        $bottom = [Math]::Min($bitmap.Height, $top + [Math]::Max(1, $Height))
        if ($right -le $left -or $bottom -le $top) {
            throw "截图区域越界：$Path X=$X Y=$Y Width=$Width Height=$Height"
        }

        $stepX = [Math]::Max(1, [int](($right - $left) / 96))
        $stepY = [Math]::Max(1, [int](($bottom - $top) / 96))
        [int64]$dark = 0
        [int64]$total = 0
        for ($y = $top; $y -lt $bottom; $y += $stepY) {
            for ($x = $left; $x -lt $right; $x += $stepX) {
                $pixel = $bitmap.GetPixel($x, $y)
                $brightness = [int](($pixel.R + $pixel.G + $pixel.B) / 3)
                if ($brightness -lt 204) {
                    $dark += 1
                }
                $total += 1
            }
        }
        if ($total -eq 0) {
            return 0.0
        }
        return [double]$dark / [double]$total
    } finally {
        $bitmap.Dispose()
    }
}

function Assert-ImageRegionDetail {
    param(
        [string]$Path,
        [string]$Label,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [double]$MinDarkRatio
    )

    $darkRatio = Get-ImageDarkRatio -Path $Path -X $X -Y $Y -Width $Width -Height $Height
    if ($darkRatio -lt $MinDarkRatio) {
        throw "截图区域疑似缺少 UI 细节：$Label Path=$Path DarkRatio=$darkRatio Min=$MinDarkRatio"
    }
}

function Get-ImageDifferenceCount {
    param(
        [string]$LeftPath,
        [string]$RightPath
    )

    $leftBitmap = [System.Drawing.Bitmap]::FromFile($LeftPath)
    $rightBitmap = [System.Drawing.Bitmap]::FromFile($RightPath)
    try {
        $width = [Math]::Min($leftBitmap.Width, $rightBitmap.Width)
        $height = [Math]::Min($leftBitmap.Height, $rightBitmap.Height)
        if ($width -le 0 -or $height -le 0) {
            return 0
        }

        $stepX = [Math]::Max(1, [int]($width / 900))
        $stepY = [Math]::Max(1, [int]($height / 700))
        [int64]$different = 0
        for ($y = 0; $y -lt $height; $y += $stepY) {
            for ($x = 0; $x -lt $width; $x += $stepX) {
                $leftPixel = $leftBitmap.GetPixel($x, $y)
                $rightPixel = $rightBitmap.GetPixel($x, $y)
                $delta =
                    [Math]::Abs([int]$leftPixel.R - [int]$rightPixel.R) +
                    [Math]::Abs([int]$leftPixel.G - [int]$rightPixel.G) +
                    [Math]::Abs([int]$leftPixel.B - [int]$rightPixel.B)
                if ($delta -gt 24) {
                    $different += 1
                }
            }
        }
        return $different
    } finally {
        $leftBitmap.Dispose()
        $rightBitmap.Dispose()
    }
}

function Assert-ImagesDiffer {
    param(
        [string]$LeftPath,
        [string]$RightPath,
        [string]$Label,
        [int]$MinDifferentPixels
    )

    $different = Get-ImageDifferenceCount -LeftPath $LeftPath -RightPath $RightPath
    if ($different -lt $MinDifferentPixels) {
        throw "截图差异不足：$Label Difference=$different Min=$MinDifferentPixels Left=$LeftPath Right=$RightPath"
    }
}

function Wait-MainWindow {
    param(
        [System.Diagnostics.Process]$Process,
        [int]$TimeoutSeconds = 30
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "程序提前退出，ExitCode=$($Process.ExitCode)"
        }
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }
        Start-Sleep -Milliseconds 300
    }
    throw "等待主窗口超时：$WindowTitle"
}

function Get-NativeDpiMetrics {
    param([IntPtr]$WindowHandle)

    [uint32]$windowDpi = 0
    try {
        $windowDpi = [NativeUiCapture]::GetDpiForWindow($WindowHandle)
    } catch {
        $windowDpi = 0
    }

    [int]$screenDpiX = 0
    [int]$screenDpiY = 0
    $screenDc = [NativeUiCapture]::GetDC([IntPtr]::Zero)
    if ($screenDc -ne [IntPtr]::Zero) {
        try {
            $screenDpiX = [NativeUiCapture]::GetDeviceCaps($screenDc, [NativeUiCapture]::LOGPIXELSX)
            $screenDpiY = [NativeUiCapture]::GetDeviceCaps($screenDc, [NativeUiCapture]::LOGPIXELSY)
        } finally {
            [NativeUiCapture]::ReleaseDC([IntPtr]::Zero, $screenDc) | Out-Null
        }
    }

    $scalePercent = if ($windowDpi -gt 0) {
        [int][Math]::Round(([double]$windowDpi * 100.0) / 96.0)
    } elseif ($screenDpiX -gt 0) {
        [int][Math]::Round(([double]$screenDpiX * 100.0) / 96.0)
    } else {
        0
    }

    [pscustomobject]@{
        WindowDpi = $windowDpi
        ScreenDpiX = $screenDpiX
        ScreenDpiY = $screenDpiY
        ScalePercent = $scalePercent
    }
}

function Select-NativeTab {
    param(
        [IntPtr]$WindowHandle,
        [int]$Index
    )

    [NativeUiCapture]::SetForegroundWindow($WindowHandle) | Out-Null

    $tabCenters = @(25, 75, 125, 175, 225)
    if ($Index -lt 0 -or $Index -ge $tabCenters.Count) {
        throw "标签页索引越界，Index=$Index"
    }

    $clientRect = New-Object NativeUiCapture+RECT
    if (-not [NativeUiCapture]::GetClientRect($WindowHandle, [ref]$clientRect)) {
        throw "GetClientRect 主窗口失败。"
    }

    $clientWidth = [Math]::Max(1, $clientRect.Right - $clientRect.Left)
    $clientHeight = [Math]::Max(1, $clientRect.Bottom - $clientRect.Top)
    $compact = $clientWidth -lt 1040 -or $clientHeight -lt 720
    $tight = $clientWidth -lt 860
    $margin = if ($tight) { 3 } elseif ($compact) { 4 } else { 6 }
    $statusHeight = if ($compact) { 20 } else { 22 }
    $sideGap = if ($tight) { 4 } elseif ($compact) { 5 } else { 6 }
    $desiredWorkHeight = if ($compact) { 230 } else { 236 }
    $minimumLogHeight = if ($compact) { 150 } else { 210 }
    $splitterHeight = 12

    $statusY = [Math]::Max($margin, $clientHeight - $statusHeight - 4)
    $contentHeight = [Math]::Max(1, $statusY - $margin)
    $maximumWorkHeight = [Math]::Max(84, $contentHeight - $minimumLogHeight - $splitterHeight)
    $workHeight = [Math]::Max(84, [Math]::Min($desiredWorkHeight, $maximumWorkHeight))
    $logY = $margin
    $logHeight = [Math]::Max(1, $statusY - $logY - $workHeight - $splitterHeight)
    $tabsY = $logY + $logHeight + $splitterHeight

    $origin = New-Object NativeUiCapture+POINT
    $origin.X = 0
    $origin.Y = 0
    if (-not [NativeUiCapture]::ClientToScreen($WindowHandle, [ref]$origin)) {
        throw "ClientToScreen 主窗口失败。"
    }

    $screenX = $origin.X + $margin + $tabCenters[$Index]
    $screenY = $origin.Y + $tabsY + 14
    Write-Host "点击标签页：Index=$Index X=$screenX Y=$screenY Client=${clientWidth}x${clientHeight}"
    [NativeUiCapture]::SetCursorPos($screenX, $screenY) | Out-Null
    Start-Sleep -Milliseconds 80
    [NativeUiCapture]::mouse_event([NativeUiCapture]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [NativeUiCapture]::mouse_event([NativeUiCapture]::MOUSEEVENTF_LEFTUP, 0, 0, 0, [UIntPtr]::Zero)

    Start-Sleep -Milliseconds 650
}

function Capture-TabSet {
    param(
        [IntPtr]$WindowHandle,
        [string]$Prefix,
        [int]$Width,
        [int]$Height
    )

    [NativeUiCapture]::ShowWindow($WindowHandle, [NativeUiCapture]::SW_RESTORE) | Out-Null
    $setWindowFlags = [NativeUiCapture]::SWP_NOZORDER -bor [NativeUiCapture]::SWP_SHOWWINDOW
    [NativeUiCapture]::SetWindowPos(
        $WindowHandle,
        [IntPtr]::Zero,
        0,
        0,
        $Width,
        $Height,
        $setWindowFlags) | Out-Null
    [NativeUiCapture]::SetForegroundWindow($WindowHandle) | Out-Null
    Start-Sleep -Milliseconds 800

    $tabs = @(
        @{ Name = "single"; Index = 0 },
        @{ Name = "quick"; Index = 1 },
        @{ Name = "file"; Index = 2 },
        @{ Name = "scan"; Index = 3 },
        @{ Name = "settings"; Index = 4 }
    )

    foreach ($tab in $tabs) {
        $tabName = [string]$tab["Name"]
        $tabIndex = [int]$tab["Index"]
        Select-NativeTab -WindowHandle $WindowHandle -Index $tabIndex
        $file = Join-Path $outputPath "$Prefix$tabName.png"
        Capture-WindowImage -WindowHandle $WindowHandle -Path $file
        Assert-ImageVisible -Path $file
        Add-CaptureStatus -Scenario "tab-$tabName" -Detail "file=$Prefix$tabName.png size=${Width}x${Height}"
        Write-Host "截图完成：$file"
    }

    $singleFile = Join-Path $outputPath "${Prefix}single.png"
    $scanFile = Join-Path $outputPath "${Prefix}scan.png"
    Assert-ImagesDiffer -LeftPath $singleFile -RightPath $scanFile -Label "${Prefix}single-vs-scan" -MinDifferentPixels 300
    $setScenario = if ($Prefix -eq "compact-tab-") { "compact-tab-set" } else { "tab-set" }
    Add-CaptureStatus -Scenario $setScenario -Detail "screenshots=$($tabs.Count) switching=clicked-frame-diff-validated-by-ui-perf"
}

function Capture-ResizeSweep {
    param([IntPtr]$WindowHandle)

    [NativeUiCapture]::ShowWindow($WindowHandle, [NativeUiCapture]::SW_RESTORE) | Out-Null
    [NativeUiCapture]::SetForegroundWindow($WindowHandle) | Out-Null
    $setWindowFlags = [NativeUiCapture]::SWP_NOZORDER -bor [NativeUiCapture]::SWP_SHOWWINDOW
    $sizes = @(
        @{ Width = 980; Height = 690 },
        @{ Width = 760; Height = 520 },
        @{ Width = 1180; Height = 740 },
        @{ Width = 900; Height = 620 },
        @{ Width = $DefaultWidth; Height = $DefaultHeight }
    )

    for ($index = 0; $index -lt $sizes.Count; $index += 1) {
        $size = $sizes[$index]
        [NativeUiCapture]::SetWindowPos(
            $WindowHandle,
            [IntPtr]::Zero,
            0,
            0,
            [int]$size["Width"],
            [int]$size["Height"],
            $setWindowFlags) | Out-Null
        Start-Sleep -Milliseconds 1000
        $file = Join-Path $outputPath "resize-$index.png"
        Capture-WindowImage -WindowHandle $WindowHandle -Path $file
        Assert-ImageVisible -Path $file
        Add-CaptureStatus -Scenario "resize-$index" -Detail ("file=resize-{0}.png size={1}x{2}" -f $index, $size["Width"], $size["Height"])
        Write-Host "缩放截图完成：$file"
    }

    $finalFile = Join-Path $outputPath ("resize-{0}.png" -f ($sizes.Count - 1))
    $finalImage = [System.Drawing.Bitmap]::FromFile($finalFile)
    try {
        $finalWidth = $finalImage.Width
    } finally {
        $finalImage.Dispose()
    }
    $toolbarWidth = [Math]::Max(240, $finalWidth - 180)
    Assert-ImageRegionDetail -Path $finalFile -Label "缩放后日志工具条" -X 0 -Y 12 -Width $toolbarWidth -Height 80 -MinDarkRatio 0.05
    Assert-ImageRegionDetail -Path $finalFile -Label "缩放后串口侧栏" -X ([Math]::Max(0, $finalWidth - 260)) -Y 12 -Width 260 -Height 330 -MinDarkRatio 0.045
    Add-CaptureStatus -Scenario "resize-sweep" -Detail "screenshots=$($sizes.Count)"
}

function Capture-LogSplitterMovement {
    param([IntPtr]$WindowHandle)

    [NativeUiCapture]::ShowWindow($WindowHandle, [NativeUiCapture]::SW_RESTORE) | Out-Null
    $setWindowFlags = [NativeUiCapture]::SWP_NOZORDER -bor [NativeUiCapture]::SWP_SHOWWINDOW
    [NativeUiCapture]::SetWindowPos(
        $WindowHandle,
        [IntPtr]::Zero,
        0,
        0,
        $DefaultWidth,
        $DefaultHeight,
        $setWindowFlags) | Out-Null
    [NativeUiCapture]::SetForegroundWindow($WindowHandle) | Out-Null
    Start-Sleep -Milliseconds 800

    $beforeFile = Join-Path $outputPath "log-splitter-before.png"
    $frame01File = Join-Path $outputPath "log-splitter-frame-01.png"
    $frame02File = Join-Path $outputPath "log-splitter-frame-02.png"
    $afterFile = Join-Path $outputPath "log-splitter-after.png"
    Capture-WindowImage -WindowHandle $WindowHandle -Path $beforeFile
    Assert-ImageVisible -Path $beforeFile

    $clientRect = New-Object NativeUiCapture+RECT
    if (-not [NativeUiCapture]::GetClientRect($WindowHandle, [ref]$clientRect)) {
        throw "GetClientRect 主窗口失败。"
    }
    $clientWidth = [Math]::Max(1, $clientRect.Right - $clientRect.Left)
    $clientHeight = [Math]::Max(1, $clientRect.Bottom - $clientRect.Top)
    $margin = if ($clientWidth -lt 860) { 3 } elseif ($clientWidth -lt 1040 -or $clientHeight -lt 720) { 4 } else { 6 }
    $statusHeight = if ($clientWidth -lt 1040 -or $clientHeight -lt 720) { 20 } else { 22 }
    $sideGap = if ($clientWidth -lt 860) { 4 } elseif ($clientWidth -lt 1040 -or $clientHeight -lt 720) { 5 } else { 6 }
    $desiredWorkHeight = if ($clientWidth -lt 1040 -or $clientHeight -lt 720) { 230 } else { 236 }
    $minimumLogHeight = if ($clientWidth -lt 1040 -or $clientHeight -lt 720) { 150 } else { 210 }
    $splitterHeight = 12
    $statusY = [Math]::Max($margin, $clientHeight - $statusHeight - 4)
    $contentHeight = [Math]::Max(1, $statusY - $margin)
    $maximumWorkHeight = [Math]::Max(84, $contentHeight - $minimumLogHeight - $splitterHeight)
    $workHeight = [Math]::Max(84, [Math]::Min($desiredWorkHeight, $maximumWorkHeight))
    $logHeight = [Math]::Max(1, $statusY - $margin - $workHeight - $splitterHeight)
    $splitterY = $margin + $logHeight + [Math]::Max(1, [int]($splitterHeight / 2))

    $origin = New-Object NativeUiCapture+POINT
    $origin.X = 0
    $origin.Y = 0
    if (-not [NativeUiCapture]::ClientToScreen($WindowHandle, [ref]$origin)) {
        throw "ClientToScreen 主窗口失败。"
    }

    $screenX = $origin.X + [Math]::Max(80, [int]($clientWidth / 2))
    $screenY = $origin.Y + $splitterY
    [NativeUiCapture]::SetCursorPos($screenX, $screenY) | Out-Null
    Start-Sleep -Milliseconds 80
    [NativeUiCapture]::mouse_event([NativeUiCapture]::MOUSEEVENTF_LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [NativeUiCapture]::SetCursorPos($screenX, [Math]::Max($origin.Y + 80, $screenY - 24)) | Out-Null
    Start-Sleep -Milliseconds 180
    Capture-WindowImage -WindowHandle $WindowHandle -Path $frame01File
    Assert-ImageVisible -Path $frame01File
    [NativeUiCapture]::SetCursorPos($screenX, [Math]::Max($origin.Y + 80, $screenY - 48)) | Out-Null
    Start-Sleep -Milliseconds 180
    Capture-WindowImage -WindowHandle $WindowHandle -Path $frame02File
    Assert-ImageVisible -Path $frame02File
    [NativeUiCapture]::mouse_event([NativeUiCapture]::MOUSEEVENTF_LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 900

    Capture-WindowImage -WindowHandle $WindowHandle -Path $afterFile
    Assert-ImageVisible -Path $afterFile
    Assert-ImagesDiffer -LeftPath $beforeFile -RightPath $frame01File -Label "splitter-before-vs-frame-01" -MinDifferentPixels 300
    Assert-ImagesDiffer -LeftPath $frame01File -RightPath $frame02File -Label "splitter-frame-01-vs-frame-02" -MinDifferentPixels 300
    Assert-ImagesDiffer -LeftPath $beforeFile -RightPath $afterFile -Label "splitter-before-vs-after" -MinDifferentPixels 300
    Add-CaptureStatus -Scenario "splitter-drag-frames" -Detail "files=log-splitter-before.png,log-splitter-frame-01.png,log-splitter-frame-02.png,log-splitter-after.png deltas=-24,-48 live=true diff-gated=true"
}

function Capture-DpiSmoke {
    param(
        [IntPtr]$WindowHandle,
        $DpiMetrics
    )

    [NativeUiCapture]::ShowWindow($WindowHandle, [NativeUiCapture]::SW_RESTORE) | Out-Null
    [NativeUiCapture]::SetForegroundWindow($WindowHandle) | Out-Null
    $setWindowFlags = [NativeUiCapture]::SWP_NOZORDER -bor [NativeUiCapture]::SWP_SHOWWINDOW

    foreach ($scale in @(100, 125)) {
        $width = [Math]::Max(1, [int][Math]::Round($DefaultWidth * $scale / 100.0))
        $height = [Math]::Max(1, [int][Math]::Round($DefaultHeight * $scale / 100.0))
        [NativeUiCapture]::SetWindowPos(
            $WindowHandle,
            [IntPtr]::Zero,
            0,
            0,
            $width,
            $height,
            $setWindowFlags) | Out-Null
        Start-Sleep -Milliseconds 900

        $fileName = "dpi-$scale-window.png"
        $file = Join-Path $outputPath $fileName
        Capture-WindowImage -WindowHandle $WindowHandle -Path $file
        Assert-ImageVisible -Path $file
        Add-CaptureStatus `
            -Scenario "dpi-smoke-$scale" `
            -Detail "file=$fileName requested-scale=$scale actual-window-dpi=$($DpiMetrics.WindowDpi) actual-scale=$($DpiMetrics.ScalePercent) direct-dpi-switch=unavailable"
    }
}

Write-Host "启动：$ExePath"
$process = Start-Process -FilePath $ExePath -PassThru
try {
    $windowHandle = Wait-MainWindow -Process $process
    Set-CaptureWindowSize -WindowHandle $windowHandle -Width $DefaultWidth -Height $DefaultHeight -DelayMilliseconds 1000

    $dpiMetrics = Get-NativeDpiMetrics -WindowHandle $windowHandle
    $defaultWindowSize = Get-NativeWindowSize -WindowHandle $windowHandle
    @(
        "WindowTitle=$WindowTitle",
        "ExePath=$ExePath",
        "WindowHandle=$windowHandle",
        "DefaultSize=${DefaultWidth}x${DefaultHeight}",
        "DefaultCaptureSize=$($defaultWindowSize.Width)x$($defaultWindowSize.Height)",
        "CompactSize=${CompactWidth}x${CompactHeight}",
        "WindowDpi=$($dpiMetrics.WindowDpi)",
        "ScreenDpiX=$($dpiMetrics.ScreenDpiX)",
        "ScreenDpiY=$($dpiMetrics.ScreenDpiY)",
        "ScalePercent=$($dpiMetrics.ScalePercent)",
        "DpiSwitching=not-attempted-runner-scale-recorded-scaled-window-smoke-used",
        "CapturedAt=$((Get-Date).ToString("o"))"
    ) | Set-Content -Path (Join-Path $outputPath "window-info.txt") -Encoding UTF8

    $rootFile = Join-Path $outputPath "root.png"
    Capture-WindowImage -WindowHandle $windowHandle -Path $rootFile
    Assert-ImageVisible -Path $rootFile
    Add-CaptureStatus -Scenario "default-window" -Detail "file=root.png requested=${DefaultWidth}x${DefaultHeight} actual=$($defaultWindowSize.Width)x$($defaultWindowSize.Height)"
    $rootImage = [System.Drawing.Bitmap]::FromFile($rootFile)
    try {
        $rootWidth = $rootImage.Width
    } finally {
        $rootImage.Dispose()
    }
    $toolbarWidth = [Math]::Max(240, $rootWidth - 180)
    Assert-ImageRegionDetail -Path $rootFile -Label "日志工具条" -X 0 -Y 12 -Width $toolbarWidth -Height 80 -MinDarkRatio 0.05
    Assert-ImageRegionDetail -Path $rootFile -Label "串口侧栏" -X ([Math]::Max(0, $rootWidth - 260)) -Y 12 -Width 260 -Height 330 -MinDarkRatio 0.045

    Capture-DpiSmoke -WindowHandle $windowHandle -DpiMetrics $dpiMetrics
    Capture-TabSet -WindowHandle $windowHandle -Prefix "tab-" -Width $DefaultWidth -Height $DefaultHeight
    Capture-TabSet -WindowHandle $windowHandle -Prefix "compact-tab-" -Width $CompactWidth -Height $CompactHeight
    Capture-ResizeSweep -WindowHandle $windowHandle
    Capture-LogSplitterMovement -WindowHandle $windowHandle
    Add-CaptureStatus -Scenario "phase-1-ui-regression-closure" -Detail "tabs=5 compact-tabs=5 resize-sweep=true dpi-smoke=true splitter-drag-frames=true"
    Add-CaptureStatus -Scenario "capture-complete"
} catch {
    Add-CaptureStatus -Scenario "capture" -Status "FAIL" -Detail $_.Exception.Message
    throw
} finally {
    Write-UiEvidenceSummary
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}
