param(
    [string]$BuildDir = "build-windows-native",
    [string]$Config = "Release",
    [string]$PackageDir = "artifacts\windows-native",
    [int64]$MaxZipBytes = 5MB,
    [int64]$MaxExtractedBytes = 8MB,
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

if ([string]::IsNullOrWhiteSpace($Config)) {
    $Config = "Release"
}

$repoRoot = Resolve-RepoRoot
$buildPath = Join-Path $repoRoot $BuildDir
$packageRoot = Join-Path $repoRoot $PackageDir
$packageName = "SerialValueMatcherNative-win32-native-x64"
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

Write-Host "运行 native self-test..."
& $exePath --self-test
if ($LASTEXITCODE -ne 0) {
    throw "native self-test 失败，退出码：$LASTEXITCODE"
}

New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir | Out-Null
Copy-Item $exePath $stageDir
Copy-Item (Join-Path $repoRoot "README.md") $stageDir

$docsDir = Join-Path $stageDir "docs"
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null
$docsToCopy = @(
    "docs\windows-native-slimming.md",
    "docs\windows-native-ui-validation.md",
    "docs\windows-serial-validation.md",
    "docs\windows-deployment.md"
)
foreach ($relativeDoc in $docsToCopy) {
    $docPath = Join-Path $repoRoot $relativeDoc
    if (Test-Path $docPath) {
        Copy-Item $docPath $docsDir
    }
}

if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
if (Test-Path $summaryPath) { Remove-Item $summaryPath -Force }
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPath -Force
$hash = Get-FileHash -Algorithm SHA256 $zipPath
"SHA256 $($hash.Hash)  $(Split-Path -Leaf $zipPath)" | Set-Content -Path $hashPath -Encoding UTF8

& (Join-Path $repoRoot "scripts\inspect-windows-package.ps1") `
    -StageDir $stageDir `
    -ZipPath $zipPath `
    -SummaryPath $summaryPath `
    -MaxZipBytes $MaxZipBytes `
    -MaxExtractedBytes $MaxExtractedBytes

Write-Host "Windows native 打包完成：$zipPath"
Write-Host "SHA256：$($hash.Hash)"
Write-Host "体积摘要：$summaryPath"
