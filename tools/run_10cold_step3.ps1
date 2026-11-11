param(
    [int]$StartRun = 2,
    [int]$EndRun = 10,
    [int]$DurationSec = 60
)

$ErrorActionPreference = "Stop"
$root = (Get-Location).Path
$lockFile = Join-Path $root "build\run.lock"
$exe = Join-Path $root "build\gow_ascension.exe"
$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"

if (!(Test-Path $exe)) {
    Write-Error "Cannot find $exe"
    exit 1
}

New-Item -ItemType Directory -Force "scratch\step3" | Out-Null

for ($run = $StartRun; $run -le $EndRun; $run++) {
    $runId = "{0:D2}" -f $run
    $errLog = Join-Path $root "scratch\step3\run${runId}_stderr.log"
    $outLog = Join-Path $root "scratch\step3\run${runId}_stdout.log"

    Write-Host "`n=========================================="
    Write-Host "Starting Cold Run $run / $EndRun (60s)..."
    Write-Host "=========================================="

    # Acquire run.lock
    if (Test-Path $lockFile) {
        Remove-Item -Force $lockFile
    }
    Set-Content -Path $lockFile -Value "PID=$PID, Run=$run, Start=$(Get-Date -Format o)"

    # Cold run cleanup: FIOS zero-filled cache
    $cachePath = Join-Path $root "vfs\dev_hdd0\game\BCAS25016\cache.idx"
    if (Test-Path $cachePath) {
        try {
            $bytes = [System.IO.File]::ReadAllBytes($cachePath)
            if ($bytes.Length -gt 0) {
                $allZero = $true
                for ($i = 0; $i -lt $bytes.Length; $i++) {
                    if ($bytes[$i] -ne 0) { $allZero = $false; break }
                }
                if ($allZero) {
                    Write-Host "[FIOS-CLEAN] Removing zero-filled cache.idx before cold run"
                    Remove-Item -Force $cachePath
                }
            }
        } catch {}
    }

    if (Test-Path $errLog) { Remove-Item -Force $errLog }
    if (Test-Path $outLog) { Remove-Item -Force $outLog }

    $proc = Start-Process -FilePath $exe -ArgumentList @($elf, "--renderer=d3d12") `
        -WorkingDirectory $root `
        -RedirectStandardError $errLog `
        -RedirectStandardOutput $outLog `
        -PassThru

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $DurationSec) {
        if ($proc.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }

    if (!$proc.HasExited) {
        Write-Host "Stopping process after $($sw.Elapsed.TotalSeconds)s..."
        Stop-Process -Id $proc.Id -Force
    }

    # Release lock
    if (Test-Path $lockFile) {
        Remove-Item -Force $lockFile
    }

    Write-Host "Run $run completed. Inspecting log: $errLog"
    $c_1e48 = (Select-String -Path $errLog -Pattern '\[WWS-1E48\]' | Measure-Object).Count
    $last_3510 = (Select-String -Path $errLog -Pattern '\[0x747D3510\]=0x([0-9A-Fa-f]+)' | Select-Object -Last 1)
    $last_tele = (Select-String -Path $errLog -Pattern '\[TELEMETRY 5s\]' | Select-Object -Last 1)
    $last_draw = (Select-String -Path $errLog -Pattern '\[live-draw\]' | Select-Object -Last 1)

    Write-Host "  WWS-1E48 count: $c_1e48"
    Write-Host "  Last 0x747D3510 line: $last_3510"
    Write-Host "  Last Telemetry: $last_tele"
    Write-Host "  Last LiveDraw: $last_draw"

    # Verify disk space
    $drive = Get-PSDrive C
    Write-Host "  Disk C: Free = $($drive.Free / 1GB) GB"
}
