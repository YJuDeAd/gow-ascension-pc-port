$env:GOW_NO_WWS_HACK = "1"
$env:RSX_LIVE_DRAW = "1"
$env:SPU_INTERP_UNLIFTED = "1"
$env:SPU_DMA_LAX = "1"
$env:GOW_PAD_AUTOSTART = "1"
$env:PAD_ALWAYS_CHANGE = "1"
$env:PS3_HDD0_ROOT = "vfs"
$env:PS3_VFS_ROOT = "E:"
$env:GOW_WATCHDOG = "1"
$env:GOW_7B1C_TIMEOUT_SEC = "2"
$env:GOW_FIOS_CACHE = "off"
if (Test-Path "mods\skip-intro") {
    $env:PS3_MODS = (Resolve-Path "mods\skip-intro").Path
}

$stdoutLog = "build\logs\phase16_test.out"
$stderrLog = "build\logs\phase16_test.err"
if (Test-Path $stdoutLog) { Remove-Item $stdoutLog -Force }
if (Test-Path $stderrLog) { Remove-Item $stderrLog -Force }

$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList "vfs\PS3_GAME\USRDIR\EBOOT.ELF" -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

Write-Host "Running for 75s with GOW_7B1C_TIMEOUT_SEC=2..."
Start-Sleep -Seconds 75
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Write-Host "Process finished."
