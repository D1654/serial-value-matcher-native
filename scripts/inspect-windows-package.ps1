param(
    [Parameter(Mandatory = $true)][string]$StageDir,
    [Parameter(Mandatory = $true)][string]$ZipPath,
    [Parameter(Mandatory = $true)][string]$HashPath,
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

function Read-UInt16LE($Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 2 -gt $Bytes.Length) {
        throw "PE 偏移越界：UInt16 Offset=$Offset"
    }
    return [System.BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-UInt32LE($Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw "PE 偏移越界：UInt32 Offset=$Offset"
    }
    return [System.BitConverter]::ToUInt32($Bytes, $Offset)
}

function Convert-RvaToFileOffset($Rva, $Sections) {
    foreach ($section in $Sections) {
        $span = [Math]::Max([int64]$section.VirtualSize, [int64]$section.SizeOfRawData)
        if ($span -le 0) {
            continue
        }
        if ($Rva -ge $section.VirtualAddress -and $Rva -lt ($section.VirtualAddress + $span)) {
            return [int]($section.PointerToRawData + ($Rva - $section.VirtualAddress))
        }
    }
    return -1
}

function Read-AsciiNullTerminated($Bytes, [int]$Offset) {
    if ($Offset -lt 0 -or $Offset -ge $Bytes.Length) {
        throw "PE 字符串偏移越界：Offset=$Offset"
    }
    $end = $Offset
    while ($end -lt $Bytes.Length -and $Bytes[$end] -ne 0) {
        $end += 1
    }
    return [System.Text.Encoding]::ASCII.GetString($Bytes, $Offset, $end - $Offset)
}

function Get-PeImportedDlls($Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "不是有效 PE 文件：$Path"
    }

    $peOffset = [int](Read-UInt32LE $bytes 0x3C)
    if ($peOffset + 24 -gt $bytes.Length) {
        throw "PE 头越界：$Path"
    }
    if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "PE 签名无效：$Path"
    }

    $sectionCount = [int](Read-UInt16LE $bytes ($peOffset + 6))
    $optionalHeaderSize = [int](Read-UInt16LE $bytes ($peOffset + 20))
    $optionalOffset = $peOffset + 24
    $magic = Read-UInt16LE $bytes $optionalOffset
    if ($magic -eq 0x20B) {
        $dataDirectoryOffset = $optionalOffset + 112
    } elseif ($magic -eq 0x10B) {
        $dataDirectoryOffset = $optionalOffset + 96
    } else {
        throw "未知 PE optional header magic：$magic"
    }

    $importDirectoryOffset = $dataDirectoryOffset + 8
    $importRva = Read-UInt32LE $bytes $importDirectoryOffset
    if ($importRva -eq 0) {
        return @()
    }

    $sections = New-Object System.Collections.Generic.List[object]
    $sectionOffset = $optionalOffset + $optionalHeaderSize
    for ($index = 0; $index -lt $sectionCount; $index += 1) {
        $offset = $sectionOffset + $index * 40
        $sections.Add([pscustomobject]@{
            VirtualSize = Read-UInt32LE $bytes ($offset + 8)
            VirtualAddress = Read-UInt32LE $bytes ($offset + 12)
            SizeOfRawData = Read-UInt32LE $bytes ($offset + 16)
            PointerToRawData = Read-UInt32LE $bytes ($offset + 20)
        })
    }

    $descriptorOffset = Convert-RvaToFileOffset $importRva $sections
    if ($descriptorOffset -lt 0) {
        throw "无法定位 PE import directory：RVA=$importRva"
    }

    $imports = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    for ($offset = $descriptorOffset; $offset + 20 -le $bytes.Length; $offset += 20) {
        $originalFirstThunk = Read-UInt32LE $bytes $offset
        $nameRva = Read-UInt32LE $bytes ($offset + 12)
        $firstThunk = Read-UInt32LE $bytes ($offset + 16)
        if ($originalFirstThunk -eq 0 -and $nameRva -eq 0 -and $firstThunk -eq 0) {
            break
        }
        $nameOffset = Convert-RvaToFileOffset $nameRva $sections
        if ($nameOffset -lt 0) {
            throw "无法定位 PE import name：RVA=$nameRva"
        }
        [void]$imports.Add((Read-AsciiNullTerminated $bytes $nameOffset))
    }

    return @($imports | Sort-Object)
}

if (-not (Test-Path $StageDir)) {
    throw "包检查失败：目录不存在：$StageDir"
}
if (-not (Test-Path $ZipPath)) {
    throw "包检查失败：zip 不存在：$ZipPath"
}
if (-not (Test-Path $HashPath)) {
    throw "包检查失败：SHA256 文件不存在：$HashPath"
}

$packageFiles = @(Get-ChildItem -Path $StageDir -Recurse -File)
$packageBytes = Get-FileLengthSum $packageFiles
$zipBytes = (Get-Item $ZipPath).Length
$zipHash = (Get-FileHash -Algorithm SHA256 $ZipPath).Hash.ToUpperInvariant()
$hashText = (Get-Content -Path $HashPath -Raw -Encoding UTF8).ToUpperInvariant()
$hashMatches = $hashText.Contains($zipHash)
$largestFiles = $packageFiles | Sort-Object -Property Length -Descending | Select-Object -First 12
$forbiddenFiles = @($packageFiles | Where-Object {
    $_.Name -like "Qt6*.dll" -or
    $_.Name -ieq "qsqlite.dll" -or
    $_.Name -ieq "mscoree.dll" -or
    $_.Name -ieq "coreclr.dll" -or
    $_.Name -ieq "clrjit.dll" -or
    $_.Name -ieq "hostfxr.dll" -or
    $_.Name -ieq "hostpolicy.dll" -or
    $_.Name -like "System.*.dll" -or
    $_.Name -like "Microsoft.NET*.dll" -or
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
    "HEX+文本",
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
$nativeExeHash = ""
$importedDlls = @()
$importScanError = $null
if ($null -eq $nativeExe) {
    foreach ($term in $unicodeProbeTerms) {
        $missingUnicodeTerms.Add($term)
    }
} else {
    $nativeExeHash = (Get-FileHash -Algorithm SHA256 $nativeExe.FullName).Hash.ToUpperInvariant()
    try {
        $importedDlls = @(Get-PeImportedDlls $nativeExe.FullName)
    } catch {
        $importScanError = $_.Exception.Message
    }
    $exeBytes = [System.IO.File]::ReadAllBytes($nativeExe.FullName)
    foreach ($term in $unicodeProbeTerms) {
        $needle = [System.Text.Encoding]::Unicode.GetBytes($term)
        if (-not (Test-ByteSequence $exeBytes $needle)) {
            $missingUnicodeTerms.Add($term)
        }
    }
}
$forbiddenImportNames = @(
    "qt6core.dll",
    "qt6gui.dll",
    "qt6widgets.dll",
    "qt6serialport.dll",
    "qt6sql.dll",
    "qsqlite.dll",
    "mscoree.dll"
)
$forbiddenImports = @($importedDlls | Where-Object { $forbiddenImportNames -contains $_.ToLowerInvariant() })

$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("SerialValueMatcher Native Windows package summary")
$summary.Add("Package kind: Win32 native")
$summary.Add("Inspector: PowerShell CI")
$summary.Add("Zip path: $ZipPath")
$summary.Add("Hash path: $HashPath")
$summary.Add("Zip bytes: $zipBytes")
$summary.Add("Zip sha256: $zipHash")
$summary.Add("Zip sha256 file matches: $(if ($hashMatches) { 'yes' } else { 'no' })")
$summary.Add("Extracted bytes: $packageBytes")
$summary.Add("File count: $($packageFiles.Count)")
$summary.Add("Max zip bytes: $MaxZipBytes")
$summary.Add("Max extracted bytes: $MaxExtractedBytes")
if ($null -eq $nativeExe) {
    $summary.Add("Native exe present: no")
} else {
    $summary.Add("Native exe present: yes")
    $summary.Add("Native exe bytes: $($nativeExe.Length)")
    $summary.Add("Native exe sha256: $nativeExeHash")
}
$summary.Add("")
$summary.Add("Largest files:")
foreach ($file in $largestFiles) {
    $summary.Add(("  {0,12}  {1}" -f $file.Length, (Get-RelativePackagePath $StageDir $file.FullName)))
}
$summary.Add("")
$summary.Add("Imported DLLs:")
if ($null -ne $importScanError) {
    $summary.Add("  unavailable: $importScanError")
} elseif ($importedDlls.Count -eq 0) {
    $summary.Add("  none")
} else {
    foreach ($dll in $importedDlls) {
        $summary.Add("  $dll")
    }
}
$summary.Add("")
$summary.Add("Forbidden Qt/SQLite/.NET runtime files:")
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
    $gateFailures.Add("native 包中出现 Qt、SQLite 或 .NET 运行时文件。")
}
if ($null -eq $nativeExe) {
    $gateFailures.Add("native 包缺少 svm-native-win32.exe。")
} elseif ($missingUnicodeTerms.Count -gt 0) {
    $gateFailures.Add("native exe 缺少关键中文 UTF-16LE 文本：$($missingUnicodeTerms -join ', ')。")
}
if (-not $hashMatches) {
    $gateFailures.Add("zip SHA256 文件缺失或内容不匹配。")
}
if ($null -ne $importScanError) {
    $gateFailures.Add("native exe 导入表检查失败：$importScanError")
}
if ($forbiddenImports.Count -gt 0) {
    $gateFailures.Add("native exe 导入了禁止运行时：$($forbiddenImports -join ', ')。")
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
