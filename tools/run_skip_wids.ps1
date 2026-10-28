param(
    [int]$Seconds = 60,
    [string]$Cache = "cold"
)
$env:GOW_DIAG_SKIP_WIDS = "0-7"
Write-Host "Running smoke test with GOW_DIAG_SKIP_WIDS=$env:GOW_DIAG_SKIP_WIDS (Seconds=$Seconds, Cache=$Cache)..."
& .\tools\smoke.ps1 -Seconds $Seconds -Cache $Cache
