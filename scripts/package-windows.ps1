param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$PackageDir = "artifacts\windows",
    [string]$QtBinDir = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..")).Path
}

function Require-Command($Name, $Hint) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        throw "未找到命令 $Name。$Hint"
    }
    return $cmd.Source
}

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

if ([string]::IsNullOrWhiteSpace($Config)) {
    $Config = "Release"
}

$repoRoot = Resolve-RepoRoot
$buildPath = Join-Path $repoRoot $BuildDir
$packageRoot = Join-Path $repoRoot $PackageDir
$stageDir = Join-Path $packageRoot "SerialValueMatcherNative-win-x64"
$zipPath = Join-Path $packageRoot "SerialValueMatcherNative-win-x64.zip"
$hashPath = "$zipPath.sha256.txt"
$summaryPath = Join-Path $packageRoot "SerialValueMatcherNative-win-x64.package-summary.txt"

Write-Host "仓库目录：$repoRoot"
Write-Host "构建目录：$buildPath"
Write-Host "输出目录：$packageRoot"

if (-not $SkipBuild) {
    Require-Command "cmake" "请先安装 CMake 并加入 PATH。" | Out-Null
    cmake --build $buildPath --config $Config
}

$exeCandidates = @(
    (Join-Path $buildPath "$Config\svm-native.exe"),
    (Join-Path $buildPath "svm-native.exe")
)
$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exePath) {
    throw "未找到 svm-native.exe。请确认已完成 Windows Release 构建。"
}
Write-Host "可执行文件：$exePath"

$windeployqt = $null
if ($QtBinDir) {
    $candidate = Join-Path $QtBinDir "windeployqt.exe"
    if (Test-Path $candidate) { $windeployqt = $candidate }
}
if (-not $windeployqt) {
    $cmd = Get-Command "windeployqt.exe" -ErrorAction SilentlyContinue
    if ($cmd) { $windeployqt = $cmd.Source }
}
if (-not $windeployqt) {
    throw "未找到 windeployqt.exe。请把 Qt bin 目录加入 PATH，或通过 -QtBinDir 指定。"
}
Write-Host "windeployqt：$windeployqt"

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir | Out-Null
Copy-Item $exePath $stageDir

& $windeployqt --release --compiler-runtime --no-translations --dir $stageDir (Join-Path $stageDir "svm-native.exe")

$requiredPaths = @(
    "platforms\qwindows.dll",
    "sqldrivers\qsqlite.dll"
)
foreach ($relative in $requiredPaths) {
    $fullPath = Join-Path $stageDir $relative
    if (-not (Test-Path $fullPath)) {
        throw "打包校验失败：缺少 $relative。请检查 Qt SQL driver / platform plugin 是否已部署。"
    }
}

Copy-Item (Join-Path $repoRoot "README.md") $stageDir
if (Test-Path (Join-Path $repoRoot "docs\windows-deployment.md")) {
    New-Item -ItemType Directory -Path (Join-Path $stageDir "docs") -Force | Out-Null
    Copy-Item (Join-Path $repoRoot "docs\windows-deployment.md") (Join-Path $stageDir "docs")
}

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
if (Test-Path $summaryPath) { Remove-Item $summaryPath -Force }
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force
$hash = Get-FileHash -Algorithm SHA256 $zipPath
"SHA256 $($hash.Hash)  $(Split-Path -Leaf $zipPath)" | Set-Content -Path $hashPath -Encoding UTF8

$packageFiles = @(Get-ChildItem -Path $stageDir -Recurse -File)
$packageBytes = Get-FileLengthSum $packageFiles
$zipBytes = (Get-Item $zipPath).Length
$largestFiles = $packageFiles | Sort-Object -Property Length -Descending | Select-Object -First 12
$qtDlls = @($packageFiles | Where-Object { $_.Name -like "Qt6*.dll" } | Sort-Object -Property Name)

$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("SerialValueMatcher Native Windows Qt package summary")
$summary.Add("Package kind: Qt baseline")
$summary.Add("Zip path: $zipPath")
$summary.Add("Zip bytes: $zipBytes")
$summary.Add("Extracted bytes: $packageBytes")
$summary.Add("File count: $($packageFiles.Count)")
$summary.Add("SHA256: $($hash.Hash)")
$summary.Add("")
$summary.Add("Largest files:")
foreach ($file in $largestFiles) {
    $summary.Add(("  {0,12}  {1}" -f $file.Length, (Get-RelativePackagePath $stageDir $file.FullName)))
}
$summary.Add("")
$summary.Add("Qt DLLs:")
if ($qtDlls.Count -eq 0) {
    $summary.Add("  none")
} else {
    foreach ($file in $qtDlls) {
        $summary.Add(("  {0,12}  {1}" -f $file.Length, (Get-RelativePackagePath $stageDir $file.FullName)))
    }
}
$summary.Add("")
$summary.Add("Native slimming gate:")
$summary.Add("  Qt baseline packages may contain Qt DLLs.")
$summary.Add("  Future Win32 native packages must not contain Qt6*.dll.")
$summary.Add("  First native gate: zip <= 5 MB, extracted <= 8 MB.")
$summary.Add("  Stretch native gate: zip <= 2 MB, extracted <= 3 MB.")
$summary | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "Windows 打包完成：$zipPath"
Write-Host "SHA256：$($hash.Hash)"
Write-Host "体积摘要：$summaryPath"
Write-Host "Zip bytes：$zipBytes"
Write-Host "Extracted bytes：$packageBytes"
