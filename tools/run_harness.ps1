param(
    [string]$ExePath,
    [string]$WorkDir,
    [string]$StdErrLog,
    [string]$StdOutLog,
    [int]$DurationSec = 40
)

$logDir = [System.IO.Path]::GetDirectoryName($StdErrLog)
if ($logDir -and !(Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}

$cachePath = Join-Path $WorkDir "vfs\dev_hdd0\game\BCAS25016\cache.idx"
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

$resolvedExe = (Resolve-Path $ExePath).Path
$resolvedWork = (Resolve-Path $WorkDir).Path
$resolvedErr = [System.IO.Path]::GetFullPath($StdErrLog)
$resolvedOut = [System.IO.Path]::GetFullPath($StdOutLog)

if (Test-Path $resolvedErr) { Remove-Item -Force $resolvedErr }
if (Test-Path $resolvedOut) { Remove-Item -Force $resolvedOut }

Write-Host "Launching: $resolvedExe vfs\PS3_GAME\USRDIR\EBOOT.ELF from $resolvedWork"
$p = Start-Process -FilePath $resolvedExe -ArgumentList "vfs\PS3_GAME\USRDIR\EBOOT.ELF" `
    -WorkingDirectory $resolvedWork `
    -RedirectStandardError $resolvedErr `
    -RedirectStandardOutput $resolvedOut `
    -PassThru

$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $DurationSec) {
    if ($p.HasExited) { break }
    Start-Sleep -Milliseconds 250
}

if (!$p.HasExited) {
    Write-Host "Duration reached ($DurationSec s). Stopping process $($p.Id)..."
    Stop-Process -Id $p.Id -Force
    $p.WaitForExit(3000)
}

Start-Sleep -Milliseconds 500
Write-Host "Done. Logs written to $resolvedErr and $resolvedOut"
