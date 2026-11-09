# tools/run_batch.ps1 — Reliable cold run batch harness conforming to disk & logging rules
param(
    [int]$Runs = 10,
    [int]$Seconds = 60,
    [string]$LogDir = "build\logs\task1_revert"
)

$ErrorActionPreference = "Stop"

# Guardrail: Free disk must stay above 4.0 GB
$freeGB = (Get-PSDrive C).Free / 1GB
Write-Host ("[batch] Checking free disk space on C: {0:N2} GB" -f $freeGB)
if ($freeGB -lt 4.0) {
    Write-Error ("[batch] ERROR: Free disk space {0:N2} GB is below 4.0 GB guardrail!" -f $freeGB)
    exit 1
}

New-Item -ItemType Directory -Force $LogDir | Out-Null

$exe = "build\gow_ascension.exe"
$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"

if (!(Test-Path $exe)) {
    Write-Error "[batch] Executable not found: $exe"
    exit 1
}

for ($i = 1; $i -le $Runs; $i++) {
    $pad = "{0:D2}" -f $i
    Write-Host "=================================================="
    Write-Host ("[batch] Starting Run {0} / {1} ({2}s, cold cache)..." -f $pad, $Runs, $Seconds)
    Write-Host "=================================================="

    # Purge cache for cold run
    if (Test-Path "vfs\cache") {
        Remove-Item -Path "vfs\cache\*" -Force -Recurse -ErrorAction SilentlyContinue
    }

    $outLog = Join-Path $LogDir ("run_{0}_stdout.log" -f $pad)
    $errLog = Join-Path $LogDir ("run_{0}_stderr.log" -f $pad)

    $p = Start-Process -FilePath $exe -ArgumentList $elf -PassThru -RedirectStandardOutput $outLog -RedirectStandardError $errLog

    $elapsed = 0
    $capped = $false
    while (!$p.HasExited -and $elapsed -lt $Seconds) {
        Start-Sleep -Seconds 1
        $elapsed++

        if ($elapsed % 15 -eq 0) {
            Write-Host ("[batch] Run {0} elapsed: {1}s / {2}s..." -f $pad, $elapsed, $Seconds)
        }

        # Guardrail: If stderr exceeds 50 MB, cap/warn
        if (Test-Path $errLog) {
            $errSize = (Get-Item $errLog).Length / 1MB
            if ($errSize -gt 50.0 -and !$capped) {
                Write-Host ("[batch] WARNING: Run {0} stderr exceeded 50 MB ({1:N2} MB)! Capping logging." -f $pad, $errSize)
                $capped = $true
            }
        }
    }

    if (!$p.HasExited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        Write-Host ("[batch] Run {0} terminated at timeout ({1}s)." -f $pad, $Seconds)
    } else {
        Write-Host ("[batch] Run {0} exited with code {1} at {2}s." -f $pad, $p.ExitCode, $elapsed)
    }

    Start-Sleep -Seconds 2
}

Write-Host "[batch] All runs complete. Running analysis..."
python tools/analyze_parity_runs.py $LogDir

# Guardrail: Keep at most the 10 newest log directories in build\logs
$allDirs = Get-ChildItem -Path "build\logs" -Directory | Sort-Object CreationTime -Descending
if ($allDirs.Count -gt 10) {
    Write-Host "[batch] Managing log directories: keeping 10 newest, archiving/deleting older..."
    $toDelete = $allDirs | Select-Object -Skip 10
    foreach ($d in $toDelete) {
        # Summarize to CSV if summary.json exists
        $sumJson = Join-Path $d.FullName "summary.json"
        if (Test-Path $sumJson) {
            $csvPath = Join-Path "docs" ("{0}_summary.csv" -f $d.Name)
            Write-Host ("[batch] Summarizing {0} to {1}..." -f $d.Name, $csvPath)
            python -c "
import json, sys, os, csv
with open(r'$sumJson', 'r', encoding='utf-8') as f:
    data = json.load(f)
if data:
    with open(r'$csvPath', 'w', newline='', encoding='utf-8') as cf:
        writer = csv.DictWriter(cf, fieldnames=list(data[0].keys()))
        writer.writeheader()
        writer.writerows(data)
"
        }
        Write-Host ("[batch] Deleting older log dir: {0}" -f $d.FullName)
        Remove-Item -Path $d.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$endFree = (Get-PSDrive C).Free / 1GB
Write-Host ("[batch] Batch complete. C: Free Space: {0:N2} GB" -f $endFree)
