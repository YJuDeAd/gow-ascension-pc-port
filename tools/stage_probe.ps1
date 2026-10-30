param(
    [int]$Runs = 20,
    [int]$Seconds = 60,
    [ValidateSet("cold", "warm", "keep")][string]$Cache = "cold",
    [int]$SingleRun = 0,
    [switch]$Watchdog
)

$ErrorActionPreference = "Continue"

$exe = "build\gow_ascension.exe"
$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"

if (!(Test-Path $exe)) {
    Write-Error "Executable $exe not found!"
    exit 1
}

$logDir = "build\logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$results = @()
$runCount = if ($SingleRun -gt 0) { 1 } else { $Runs }

for ($i = 1; $i -le $runCount; $i++) {
    $runId = if ($SingleRun -gt 0) { $SingleRun } else { $i }
    Write-Host "`n>>> [STAGE_PROBE] Starting Run $runId of $runCount ($Seconds s, Cache=$Cache)..."

    # Cold cache handling
    if ($Cache -eq "cold") {
        if (Test-Path "vfs\cache") {
            Remove-Item -Path "vfs\cache\*" -Force -Recurse -ErrorAction SilentlyContinue
        }
    }

    # Environment setup
    $env:GOW_NO_WWS_HACK = "1"
    $env:RSX_LIVE_DRAW = "1"
    $env:SPU_INTERP_UNLIFTED = "1"
    $env:SPU_DMA_LAX = "1"
    $env:GOW_PAD_AUTOSTART = "1"
    $env:PAD_ALWAYS_CHANGE = "1"
    $env:PS3_HDD0_ROOT = "vfs"
    $env:PS3_HDD1_ROOT = (Resolve-Path "vfs").Path
    $env:AUDIO_RATE = "1"
    $env:AUDIO_PEAK = "1"
    $env:PS3_VFS_ROOT = "E:"
    if (Test-Path "mods\skip-intro") {
        $env:PS3_MODS = (Resolve-Path "mods\skip-intro").Path
    }
    if ($Watchdog -or $env:GOW_WATCHDOG -eq "1") {
        $env:GOW_WATCHDOG = "1"
    } else {
        $env:GOW_WATCHDOG = "0"
    }

    $stdoutLog = "$logDir\stage_run_${runId}_stdout.log"
    $stderrLog = "$logDir\stage_run_${runId}_stderr.log"
    if (Test-Path $stdoutLog) { Remove-Item $stdoutLog -Force }
    if (Test-Path $stderrLog) { Remove-Item $stderrLog -Force }

    $proc = Start-Process -FilePath $exe -ArgumentList $elf -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
    
    # Wait for the duration
    Start-Sleep -Seconds $Seconds
    
    # Terminate process cleanly
    if (!$proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }

    # Analyze stderr
    $stage = "F"
    $maxDraws = 0
    $leftTime = "never"
    $spuIntCount = 0
    $distinctLrs = @()
    $lastLr = ""

    if (Test-Path $stderrLog) {
        $lines = Get-Content $stderrLog -ErrorAction SilentlyContinue
        $spuIntCount = ($lines | Select-String "\[spu-int\] TAKEN").Count

        $reached0061B9FC = $false
        $left0061B9FC = $false

        foreach ($sl in $lines) {
            if ($sl -match '\[STAGE\]\s+t=(\d+)\s+main_lr=(0x[0-9a-fA-F]+)\s+main_last_hle=(\S+)\s+live_draws=(\d+)\s+presents=(\d+)') {
                $t = [int]$matches[1]
                $lr = $matches[2].ToLower()
                $hle = $matches[3]
                $draws = [int]$matches[4]
                $presents = [int]$matches[5]

                if ($draws -gt $maxDraws) { $maxDraws = $draws }

                if ($distinctLrs.Count -eq 0 -or $distinctLrs[-1].lr -ne $lr) {
                    $distinctLrs += [PSCustomObject]@{ t = $t; lr = $lr }
                }
                $lastLr = $lr

                if ($lr -eq "0x0061b9fc") {
                    $reached0061B9FC = $true
                } elseif ($reached0061B9FC -and !$left0061B9FC) {
                    $left0061B9FC = $true
                    $leftTime = "${t}s"
                }
            }
        }

        # Stage classification
        $lrNum = 0
        if ($lastLr -match '0x([0-9a-fA-F]+)') {
            $lrNum = [Convert]::ToUInt32($matches[1], 16)
        }

        if ($lastLr -eq "0x0061b9fc") {
            $stage = "A"
        } elseif (($lrNum -ge 0x003C98CC -and $lrNum -le 0x003CBFFF) -or ($lrNum -ge 0x0041F4B0 -and $lrNum -le 0x0041F500) -or ($lrNum -eq 0x002AE194)) {
            $stage = "B"
        } elseif ($lastLr -eq "0x0046700c" -or $lastLr -eq "0x00467030" -or ($lrNum -ge 0x00466FE8 -and $lrNum -le 0x00467040)) {
            $stage = "D"
        } elseif ($lastLr -eq "0x004a0334" -or $lastLr -eq "0x00466740") {
            $stage = "E"
        } else {
            $stage = "F"
        }
    }

    $seqStr = ($distinctLrs | ForEach-Object { "t=$($_.t)s: $($_.lr)" }) -join ", "
    if (!$seqStr) { $seqStr = "(none)" }

    $res = [PSCustomObject]@{
        Run = $runId
        LogFile = "build\logs\stage_run_${runId}_stderr.log"
        Stage = $stage
        FinalLr = $lastLr
        LrSequence = $seqStr
        TimeLeft0061B9FC = $leftTime
        MaxLiveDraws = $maxDraws
        LogoCapture = "not measured"
        SpuIntTaken = $spuIntCount
    }
    $results += $res
    Write-Host "[STAGE_PROBE] Run ${runId}: Stage=$stage, FinalLr=$lastLr, MaxDraws=$maxDraws, Left0x61B9FC=$leftTime"
    Write-Host "             Sequence: $seqStr"
}

Write-Host "`n====================== STAGE PROBE SUMMARY ======================"
$results | Format-Table Run, LogFile, Stage, FinalLr, MaxLiveDraws, LogoCapture, TimeLeft0061B9FC -AutoSize

$countA = @($results | Where-Object { $_.Stage -eq "A" }).Count
$countB = @($results | Where-Object { $_.Stage -eq "B" }).Count
$countD = @($results | Where-Object { $_.Stage -eq "D" }).Count
$countE = @($results | Where-Object { $_.Stage -eq "E" }).Count
$countF = @($results | Where-Object { $_.Stage -eq "F" }).Count
Write-Host "Stage distribution: A=$countA, B=$countB, D=$countD, E=$countE, F=$countF (out of $($results.Count))"
