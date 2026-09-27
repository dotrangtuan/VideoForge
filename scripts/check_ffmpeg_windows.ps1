$ErrorActionPreference = "SilentlyContinue"

Write-Host "FFmpeg:" -ForegroundColor Cyan
& ffmpeg.exe -hide_banner -version | Select-Object -First 1

Write-Host ""
Write-Host "Available H.264 hardware encoders:" -ForegroundColor Cyan
$encoders = (& ffmpeg.exe -hide_banner -encoders 2>&1) -join "`n"
foreach ($name in @("h264_nvenc", "h264_qsv", "h264_amf")) {
    if ($encoders -match [regex]::Escape($name)) {
        Write-Host "  FOUND  $name" -ForegroundColor Green
    } else {
        Write-Host "  -----  $name"
    }
}
