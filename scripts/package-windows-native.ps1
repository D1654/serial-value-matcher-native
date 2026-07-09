param(
    [string]$BuildDir = "build-windows-native",
    [string]$Config = "Release",
    [string]$PackageDir = "artifacts\windows-native",
    [int64]$MaxZipBytes = 5MB,
    [int64]$MaxExtractedBytes = 8MB,
    [switch]$SkipBuild,
    [switch]$SkipSelfTest,
    [switch]$SkipUiPerfTest
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

function Read-CmakeValue($RepoRoot, $Key) {
    $versionFile = Join-Path $RepoRoot "cmake\svm_version.cmake"
    $pattern = "^\s*set\($([regex]::Escape($Key))\s+`"([^`"]*)`"\s*\)"
    foreach ($line in Get-Content -Path $versionFile -Encoding UTF8) {
        if ($line -match $pattern) {
            return $Matches[1]
        }
    }
    return ""
}

if ([string]::IsNullOrWhiteSpace($Config)) {
    $Config = "Release"
}

$repoRoot = Resolve-RepoRoot
$buildPath = Join-Path $repoRoot $BuildDir
$packageRoot = Join-Path $repoRoot $PackageDir
$packageName = Read-CmakeValue $repoRoot "SVM_PACKAGE_ARTIFACT"
if ([string]::IsNullOrWhiteSpace($packageName)) {
    $packageName = "SerialValueMatcherNative-win32-native-x64"
}
$stageDir = Join-Path $packageRoot $packageName
$zipPath = Join-Path $packageRoot "$packageName.zip"
$hashPath = "$zipPath.sha256.txt"
$summaryPath = Join-Path $packageRoot "$packageName.package-summary.txt"

Write-Host "仓库目录：$repoRoot"
Write-Host "构建目录：$buildPath"
Write-Host "输出目录：$packageRoot"

if (-not $SkipBuild) {
    Require-Command "cmake" "请先安装 CMake 并加入 PATH。" | Out-Null
    cmake --build $buildPath --config $Config --target svm-native-win32 --parallel 1
}

$exeCandidates = @(
    (Join-Path $buildPath "$Config\svm-native-win32.exe"),
    (Join-Path $buildPath "svm-native-win32.exe")
)
$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exePath) {
    throw "未找到 svm-native-win32.exe。请确认已完成 Windows native Release 构建。"
}
Write-Host "可执行文件：$exePath"

if (-not $SkipSelfTest) {
    Write-Host "运行 native self-test..."
    $selfTest = Start-Process -FilePath $exePath -ArgumentList "--self-test" -Wait -PassThru
    if ($selfTest.ExitCode -ne 0) {
        throw "native self-test 失败，退出码：$($selfTest.ExitCode)"
    }
} else {
    Write-Host "跳过 native self-test：调用方已完成独立验证。"
}

if (-not $SkipUiPerfTest) {
    Write-Host "运行 UI 性能门禁..."
    $uiPerfTest = Start-Process -FilePath $exePath -ArgumentList "--ui-perf-test" -Wait -PassThru
    if ($uiPerfTest.ExitCode -ne 0) {
        throw "UI 性能门禁失败，退出码：$($uiPerfTest.ExitCode)"
    }
} else {
    Write-Host "跳过 UI 性能门禁：调用方已完成独立验证。"
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir | Out-Null
Copy-Item $exePath $stageDir
Copy-Item (Join-Path $repoRoot "README.md") $stageDir

$docsDir = Join-Path $stageDir "docs"
if (Test-Path $docsDir) { Remove-Item $docsDir -Recurse -Force }
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null
$docsSourceDir = Join-Path $repoRoot "docs"
if (Test-Path $docsSourceDir) {
    Copy-Item (Join-Path $docsSourceDir "*") $docsDir -Recurse -Force
}

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
if (Test-Path $summaryPath) { Remove-Item $summaryPath -Force }
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force
$hash = Get-FileHash -Algorithm SHA256 $zipPath
"SHA256 $($hash.Hash)  $(Split-Path -Leaf $zipPath)" | Set-Content -Path $hashPath -Encoding UTF8

& (Join-Path $repoRoot "scripts\inspect-windows-package.ps1") `
    -StageDir $stageDir `
    -ZipPath $zipPath `
    -HashPath $hashPath `
    -SummaryPath $summaryPath `
    -MaxZipBytes $MaxZipBytes `
    -MaxExtractedBytes $MaxExtractedBytes

Write-Host "Windows native 打包完成：$zipPath"
Write-Host "SHA256：$($hash.Hash)"
Write-Host "体积摘要：$summaryPath"
