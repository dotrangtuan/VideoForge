param(
    [string]$QtDir = $env:QTDIR,
    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

if ([string]::IsNullOrWhiteSpace($QtDir)) {
    Write-Host "QtDir is missing." -ForegroundColor Yellow
    Write-Host "Example:" -ForegroundColor Yellow
    Write-Host '.\scripts\build_windows.ps1 -QtDir "C:\Qt\6.11.2\msvc2022_64"'
    exit 1
}

$QtDir = (Resolve-Path $QtDir).Path
$CMake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $CMake) {
    throw "cmake.exe was not found. Install CMake and add it to PATH."
}

$WinDeployQt = Join-Path $QtDir "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) {
    throw "windeployqt.exe was not found under $QtDir\bin"
}

$BuildDir = Join-Path $ProjectRoot "build-win"
$DistDir = Join-Path $ProjectRoot "dist-win"

if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
New-Item -ItemType Directory -Path $DistDir | Out-Null

Write-Host "[1/4] Configure" -ForegroundColor Cyan
& cmake -S . -B $BuildDir -G $Generator -A x64 "-DCMAKE_PREFIX_PATH=$QtDir"

Write-Host "[2/4] Build Release" -ForegroundColor Cyan
& cmake --build $BuildDir --config Release --parallel

$Exe = Join-Path $BuildDir "Release\VideoForgeEditor.exe"
if (-not (Test-Path $Exe)) {
    # Ninja/single-config generators put the exe directly in build-win.
    $Exe = Join-Path $BuildDir "VideoForgeEditor.exe"
}
if (-not (Test-Path $Exe)) {
    throw "VideoForgeEditor.exe was not produced."
}

Copy-Item $Exe (Join-Path $DistDir "VideoForgeEditor.exe")

Write-Host "[3/4] Deploy Qt DLLs/plugins" -ForegroundColor Cyan
& $WinDeployQt --release --compiler-runtime --no-translations `
    (Join-Path $DistDir "VideoForgeEditor.exe")

Write-Host "[4/4] Copy FFmpeg if available in PATH" -ForegroundColor Cyan
$Ffmpeg = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
$Ffprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue

if ($Ffmpeg -and $Ffprobe) {
    Copy-Item $Ffmpeg.Source (Join-Path $DistDir "ffmpeg.exe")
    Copy-Item $Ffprobe.Source (Join-Path $DistDir "ffprobe.exe")
    Write-Host "FFmpeg copied into the portable folder." -ForegroundColor Green
} else {
    Write-Host "FFmpeg was not found in PATH." -ForegroundColor Yellow
    Write-Host "Copy ffmpeg.exe and ffprobe.exe into: $DistDir" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "DONE" -ForegroundColor Green
Write-Host "Portable app folder: $DistDir"
Write-Host "Run: $DistDir\VideoForgeEditor.exe"
