# tools/run_10_cold_180.ps1 — 10 cold runs x 180s benchmark
param(
    [int]$Runs = 10,
    [int]$Seconds = 180,
    [string]$LogDir = "build\logs\parity_10_post"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force $LogDir | Out-Null

for ($i = 1; $i -le $Runs; $i++) {
    $pad = "{0:D2}" -f $i
    Write-Host "=================================================="
    Write-Host "[parity-10] Starting Run $pad / $Runs ($Seconds s, cold cache)..."
    Write-Host "=================================================="
    
    # Purge cache for cold run
    if (Test-Path "vfs\cache") {
        Remove-Item -Path "vfs\cache\*" -Force -Recurse -ErrorAction SilentlyContinue
    }
    
    $outLog = Join-Path $LogDir "run_${pad}_stdout.log"
    $errLog = Join-Path $LogDir "run_${pad}_stderr.log"
    
    $exe = "build\gow_ascension.exe"
    $elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
    
    $p = Start-Process -FilePath $exe -ArgumentList $elf -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog
    
    $elapsed = 0
    while (!$p.HasExited -and $elapsed -lt $Seconds) {
        Start-Sleep -Seconds 1
        $elapsed++
        if ($elapsed % 15 -eq 0) {
            Write-Host "[parity-10] Run $pad elapsed: ${elapsed}s / ${Seconds}s..."
        }
    }
    
    if (!$p.HasExited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Write-Host "[parity-10] Run $pad terminated at timeout ($Seconds s)."
    } else {
        Write-Host "[parity-10] Run $pad exited with code $($p.ExitCode) at ${elapsed}s."
    }
    
    Start-Sleep -Seconds 2
}

Write-Host "[parity-10] All $Runs runs complete. Running analysis..."
python tools/analyze_parity_runs.py $LogDir
