$env:GOW_7B1C_BYPASS = "0"

$stdoutLog = "run_stdout.log"
$stderrLog = "run_stderr.log"

if (Test-Path $stdoutLog) { Remove-Item -Force $stdoutLog }
if (Test-Path $stderrLog) { Remove-Item -Force $stderrLog }

Write-Host "Starting gow_ascension for 50 seconds..."
$proc = Start-Process -FilePath ".\build\gow_ascension.exe" -ArgumentList ".\vfs\PS3_GAME\USRDIR\EBOOT.ELF" -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

for ($i = 1; $i -le 50; $i++) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) {
        Write-Host "Process exited early at t=${i}s with code $($proc.ExitCode)"
        break
    }
}

if (!$proc.HasExited) {
    Write-Host "Process running at 50s, stopping gracefully..."
    Stop-Process -Id $proc.Id -Force
}
Write-Host "Done."
