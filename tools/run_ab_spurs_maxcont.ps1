# tools/run_ab_spurs_maxcont.ps1 - A/B benchmark for SPURS_MAXCONT under run.lock
param(
    [int]$DurationSec = 60
)

$ErrorActionPreference = "Stop"
$LockFile = "build\run.lock"
$ExePath = "build\gow_ascension.exe"

if (-not (Test-Path $ExePath)) {
    Write-Error "Executable not found: $ExePath"
    exit 1
}

function Run-WithLock {
    param([string]$RunName, [string]$SpursMaxCont)
    
    while (Test-Path $LockFile) {
        Write-Host "Waiting for $LockFile to clear..."
        Start-Sleep -Seconds 1
    }
    
    New-Item -ItemType File -Path $LockFile -Force | Out-Null
    try {
        $stdErrLog = "build\logs\ab_${RunName}.stderr.log"
        $stdOutLog = "build\logs\ab_${RunName}.stdout.log"
        
        Write-Host "=== Running A/B [$RunName]: SPURS_MAXCONT='$SpursMaxCont' ==="
        $env:SPURS_MAXCONT = $SpursMaxCont
        
        $p = Start-Process -FilePath $ExePath -ArgumentList "vfs\PS3_GAME\USRDIR\EBOOT.ELF" `
            -RedirectStandardError $stdErrLog -RedirectStandardOutput $stdOutLog `
            -PassThru -NoNewWindow
            
        Start-Sleep -Seconds $DurationSec
        if (-not $p.HasExited) {
            Stop-Process -Id $p.Id -Force
        }
        Write-Host "[$RunName] Finished. Logs in $stdErrLog"
    } finally {
        Remove-Item -Path $LockFile -Force -ErrorAction SilentlyContinue
    }
}

# Configuration A: gowassension main default
Run-WithLock -RunName "configA_main" -SpursMaxCont "3=1,9=5,10=5,11=5,12=5,13=3,15=5,16=5,17=5,18=5"

# Configuration B: reference gow-ascension-pc default
Run-WithLock -RunName "configB_reference" -SpursMaxCont "3=5,13=3,15=5,16=5,17=5"
