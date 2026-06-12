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

$gateFailures = New-Object System.Collections.Generic.List[string]
if ($forbiddenFiles.Count -gt 0) {
    $gateFailures.Add("native 包中出现 Qt 或 qsqlite 运行时文件。")
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
