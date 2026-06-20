param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$ZipPath,
    [Parameter(Mandatory = $true)][string]$SummaryPath,
    [int64]$MaxZipBytes = 5MB,
    [int64]$MaxExtractedBytes = 8MB
)

$ErrorActionPreference = "Stop"

function Get-FileLengthSum($Files) {
    $sum = ($Files | Measure-Object -Property Length -Sum).Sum
    if ($null -eq $sum) {
        return 0
    }
    return [int64]$sum
}

function Get-RelativePackagePath($Root, $Path) {
    $normalizedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $normalizedPath = [System.IO.Path]::GetFullPath($Path)
    if ($normalizedPath.StartsWith($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $normalizedPath.Substring($normalizedRoot.Length).TrimStart('\', '/')
    }
    return $normalizedPath
}

function Test-ByteSequence($Bytes, $Needle) {
    if ($Needle.Length -eq 0) {
        return $true
    }
    if ($Bytes.Length -lt $Needle.Length) {
        return $false
    }
    for ($index = 0; $index -le $Bytes.Length - $Needle.Length; $index++) {
        $matched = $true
        for ($needleIndex = 0; $needleIndex -lt $Needle.Length; $needleIndex++) {
            if ($Bytes[$index + $needleIndex] -ne $Needle[$needleIndex]) {
                $matched = $false
                break
            }
        }
        if ($matched) {
            return $true
        }
    }
    return $false
}

if (-not (Test-Path $StageDir)) {
    throw "包检查失败：目录不存在：$StageDir"
}
if (-not (Test-Path $ZipPath)) {
    throw "包检查失败：zip 不存在：$ZipPath"
}

$packageFiles = @(Get-ChildItem -Path $StageDir -Recurse -File)
$packageBytes = Get-FileLengthSum $packageFiles
$zipBytes = (Get-Item $ZipPath).Length
$largestFiles = $packageFiles | Sort-Object -Property Length -Descending | Select-Object -First 12
$forbiddenFiles = @($packageFiles | Where-Object {
    $_.Name -like "Qt6*.dll" -or
    $_.Name -ieq "qsqlite.dll" -or
    $_.FullName -match "\\sqldrivers\\"
} | Sort-Object -Property FullName)
$nativeExe = $packageFiles | Where-Object { $_.Name -ieq "svm-native-win32.exe" } | Select-Object -First 1

$unicodeProbeTerms = @(
    "串口值匹配器",
    "文件",
    "串口",
    "工具",
    "分析",
    "帮助",
    "串口选择",
    "功能工作区",
    "单发",
    "多发",
    "发送文件",
    "发送进度",
    "当前页提示",
    "日志缓存",
    "扫描与分析工作流",
    "通信日志",
    "流程：连接设备",
    "分析生成",
    "规则验证",
    "导出报告",
    "视图",
    "默认色系",
    "柔和色系",
    "高对比色系",
    "显示日志时间",
    "复制可见日志",
    "导出可见日志",
    "查找日志",
    "跟随最新日志",
    "模式",
    "行尾",
    "历史",
    "过滤",
    "搜索",
    "界面偏好",
    "界面偏好保存失败",
    "十六进制",
    "十进制",
    "二进制",
    "十进制字节流",
    "二进制字节流",
    "日志：HEX+文本",
    "日志时间已显示",
    "日志文本解码编码",
    "当前可见通信日志",
    "正在回看历史日志",
    "文件发送进行中",
    "停止扫描",
    "完整数据已写入 native 存储",
    "UTF-8",
    "GBK"
)
$missingUnicodeTerms = New-Object System.Collections.Generic.List[string]
if ($null -eq $nativeExe) {
    foreach ($term in $unicodeProbeTerms) {
        $missingUnicodeTerms.Add($term)
    }
} else {
    $exeBytes = [System.IO.File]::ReadAllBytes($nativeExe.FullName)
    foreach ($term in $unicodeProbeTerms) {
        $needle = [System.Text.Encoding]::Unicode.GetBytes($term)
        if (-not (Test-ByteSequence $exeBytes $needle)) {
            $missingUnicodeTerms.Add($term)
        }
    }
}

$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("SerialValueMatcher Native Windows package summary")
$summary.Add("Package kind: Win32 native")
$summary.Add("Zip path: $ZipPath")
$summary.Add("Zip bytes: $zipBytes")
$summary.Add("Extracted bytes: $packageBytes")
$summary.Add("File count: $($packageFiles.Count)")
$summary.Add("Max zip bytes: $MaxZipBytes")
$summary.Add("Max extracted bytes: $MaxExtractedBytes")
$summary.Add("")
$summary.Add("Largest files:")
foreach ($file in $largestFiles) {
    $summary.Add(("  {0,12}  {1}" -f $file.Length, (Get-RelativePackagePath $StageDir $file.FullName)))
}
$summary.Add("")
$summary.Add("Forbidden Qt/SQLite runtime files:")
if ($forbiddenFiles.Count -eq 0) {
    $summary.Add("  none")
} else {
    foreach ($file in $forbiddenFiles) {
        $summary.Add(("  {0,12}  {1}" -f $file.Length, (Get-RelativePackagePath $StageDir $file.FullName)))
    }
}
$summary.Add("")
$summary.Add("Unicode text probe:")
if ($missingUnicodeTerms.Count -eq 0) {
    $summary.Add("  passed")
    foreach ($term in $unicodeProbeTerms) {
        $summary.Add("  required: $term")
    }
} else {
    $summary.Add("  failed")
    foreach ($term in $missingUnicodeTerms) {
        $summary.Add("  missing: $term")
    }
}

$gateFailures = New-Object System.Collections.Generic.List[string]
if ($forbiddenFiles.Count -gt 0) {
    $gateFailures.Add("native 包中出现 Qt 或 qsqlite 运行时文件。")
}
if ($null -eq $nativeExe) {
    $gateFailures.Add("native 包缺少 svm-native-win32.exe。")
} elseif ($missingUnicodeTerms.Count -gt 0) {
    $gateFailures.Add("native exe 缺少关键中文 UTF-16LE 文本：$($missingUnicodeTerms -join ', ')。")
}
if ($zipBytes -gt $MaxZipBytes) {
    $gateFailures.Add("zip 体积超过门禁：$zipBytes > $MaxZipBytes。")
}
if ($packageBytes -gt $MaxExtractedBytes) {
    $gateFailures.Add("解压后体积超过门禁：$packageBytes > $MaxExtractedBytes。")
}

$summary.Add("")
if ($gateFailures.Count -eq 0) {
    $summary.Add("Gate status: passed")
} else {
    $summary.Add("Gate status: failed")
    foreach ($failure in $gateFailures) {
        $summary.Add("  - $failure")
    }
}

$summary | Set-Content -Path $SummaryPath -Encoding UTF8

if ($gateFailures.Count -gt 0) {
    throw ($gateFailures -join " ")
}

Write-Host "Native 包检查通过：zip=$zipBytes extracted=$packageBytes files=$($packageFiles.Count)"
