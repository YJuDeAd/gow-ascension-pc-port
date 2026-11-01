$env:GOW_FAKE_DONE = "0"
$env:GOW_7B1C_BYPASS = "0"

if (!(Test-Path "logs\honest_runs")) {
    New-Item -ItemType Directory -Path "logs\honest_runs" -Force | Out-Null
}

$stdoutLog = "logs\honest_runs\run_test01_out.log"
$stderrLog = "logs\honest_runs\run_test01_err.log"

if (Test-Path $stdoutLog) { Remove-Item $stdoutLog -Force }
if (Test-Path $stderrLog) { Remove-Item $stderrLog -Force }

Write-Host "Running 90s test run with GOW_FAKE_DONE=0..."
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList "vfs\PS3_GAME\USRDIR\EBOOT.ELF" -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

for ($i = 1; $i -le 90; $i++) {
    Start-Sleep -Seconds 1
    if ($proc.HasExited) {
        Write-Host "Process exited at t=${i}s with code $($proc.ExitCode)"
        break
    }
}

if (!$proc.HasExited) {
    Write-Host "Reaching 90s, terminating gracefully..."
    Stop-Process -Id $proc.Id -Force
}

Write-Host "Run finished. Analyzing log..."
python tools\analyze_run.py $stderrLog
