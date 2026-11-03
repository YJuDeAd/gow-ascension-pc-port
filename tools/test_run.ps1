$env:GOW_NO_WWS_HACK = "1"
$env:RSX_LIVE_DRAW = "1"
$env:SPU_INTERP_UNLIFTED = "1"
$env:SPU_DMA_LAX = "1"
$env:GOW_PAD_AUTOSTART = "1"
$env:PAD_ALWAYS_CHANGE = "1"
$env:PS3_HDD0_ROOT = "vfs"
$env:AUDIO_RATE = "1"
$env:AUDIO_PEAK = "1"
$env:PS3_VFS_ROOT = "E:"
$env:GOW_WATCHDOG = "1"

if (!(Test-Path "build\logs")) {
    New-Item -ItemType Directory -Path "build\logs" -Force | Out-Null
}

$stdoutLog = "build\logs\direct_stdout.log"
$stderrLog = "build\logs\direct_stderr.log"

Remove-Item -Force $stdoutLog -ErrorAction SilentlyContinue
Remove-Item -Force $stderrLog -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList "vfs\PS3_GAME\USRDIR\EBOOT.ELF" -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

for ($i = 1; $i -le 80; $i++) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) {
        Write-Host "Process exited at t=${i}s with code $($proc.ExitCode)"
        break
    }
}

if (!$proc.HasExited) {
    Write-Host "Process still running at 80s, stopping..."
    Stop-Process -Id $proc.Id -Force
}
