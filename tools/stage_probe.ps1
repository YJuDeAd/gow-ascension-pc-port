param(
    [int]$Runs = 20,
    [int]$Seconds = 60,
    [ValidateSet("cold", "warm", "keep")][string]$Cache = "cold",
    [int]$SingleRun = 0,
    [switch]$Watchdog,
    [switch]$NoMods,
    [switch]$RevertDde3f83,
    [string]$OutputDir = "build\logs",
    [string]$Exe = "build\gow_ascension.exe"
)

$ErrorActionPreference = "Continue"

$exe = $Exe
$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"

if (!(Test-Path $exe)) {
    Write-Error "Executable $exe not found!"
    exit 1
}

$logDir = $OutputDir
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
    if (!$NoMods -and $env:GOW_NO_MODS -ne "1" -and (Test-Path "mods\skip-intro")) {
        $env:PS3_MODS = (Resolve-Path "mods\skip-intro").Path
    } else {
        $env:PS3_MODS = ""
    }
    if ($Watchdog -or $env:GOW_WATCHDOG -eq "1") {
        $env:GOW_WATCHDOG = "1"
    } else {
        $env:GOW_WATCHDOG = "0"
    }
    if ($RevertDde3f83 -or $env:GOW_REVERT_DDE3F83 -eq "1") {
        $env:GOW_REVERT_DDE3F83 = "1"
    } else {
        $env:GOW_REVERT_DDE3F83 = "0"
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
        try { $proc.WaitForExit(3000) } catch {}
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
        $reached00467B1C = $false
        $time00467B1C = "never"

        if ($lines | Select-String "0x00467B1C-WAIT") {
            $reached00467B1C = $true
        }

        foreach ($sl in $lines) {
            if ($sl -match '\[0x00467B1C-WAIT\s+#\d+\s+@\s+(\d+)s\]') {
                if ($time00467B1C -eq "never") {
                    $time00467B1C = "$($matches[1])s"
                }
            }
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

                if ($lr -eq "0x00467b1c") {
                    $reached00467B1C = $true
                    if ($time00467B1C -eq "never") {
                        $time00467B1C = "${t}s"
                    }
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
        } elseif ($lastLr -eq "0x00467b1c") {
            $stage = "B (0x467B1C)"
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

    $laneAlloc30s = "not captured"
    $pmEntriesStr = "not captured"
    if (Test-Path $stderrLog) {
        $auditLines = @()
        $captureAudit = $false
        foreach ($sl in $lines) {
            if ($sl -match '\[SPU-LANE-AUDIT @ (20|30|40)s\]') {
                $captureAudit = $true
                $auditLines = @()
            } elseif ($captureAudit -and $sl -match '^\s+Lane (\d+): wid=(\d+)') {
                $auditLines += "L$($matches[1])=w$($matches[2])"
            } elseif ($captureAudit -and $sl -match '^===') {
                $captureAudit = $false
            }
        }
        if ($auditLines.Count -gt 0) {
            $laneAlloc30s = $auditLines -join " "
        }

        $pmList = @()
        foreach ($sl in $lines) {
            if ($sl -match "Workload '([^']+)' \(wid=(\d+)[^)]*\): readyCount=(\d+), pm_entries=(\d+)") {
                $wid = $matches[2]
                $pme = $matches[4]
                if ([int]$pme -gt 0 -or [int]$wid -ge 8) {
                    $pmList += "w$wid=$pme"
                }
            }
        }
        if ($pmList.Count -gt 0) {
            # Take the last status dump
            $pmEntriesStr = ($pmList[-13..-1] | Select-Object -Unique) -join " "
        }
    }

    $res = [PSCustomObject]@{
        Run = $runId
        LogFile = "$logDir\stage_run_${runId}_stderr.log"
        Stage = $stage
        FinalLr = $lastLr
        Reached0x467B1C = if ($reached00467B1C) { "YES ($time00467B1C)" } else { "NO" }
        LaneAlloc30s = $laneAlloc30s
        PmEntries = $pmEntriesStr
        LrSequence = $seqStr
        TimeLeft0061B9FC = $leftTime
        MaxLiveDraws = $maxDraws
        SpuIntTaken = $spuIntCount
    }
    $results += $res
    Write-Host "[STAGE_PROBE] Run ${runId}: Stage=$stage, FinalLr=$lastLr, Reached0x467B1C=$($res.Reached0x467B1C), MaxDraws=$maxDraws, Left0x61B9FC=$leftTime"
    Write-Host "             Lanes: $laneAlloc30s | PM: $pmEntriesStr"
    Write-Host "             Sequence: $seqStr"
}

Write-Host "`n====================== STAGE PROBE SUMMARY ======================"
$results | Format-Table Run, LogFile, Stage, FinalLr, Reached0x467B1C, LaneAlloc30s, MaxLiveDraws, TimeLeft0061B9FC -AutoSize

$countA = @($results | Where-Object { $_.Stage -eq "A" }).Count
$countB = @($results | Where-Object { $_.Stage -like "B*" }).Count
$countD = @($results | Where-Object { $_.Stage -eq "D" }).Count
$countE = @($results | Where-Object { $_.Stage -eq "E" }).Count
$countF = @($results | Where-Object { $_.Stage -eq "F" }).Count
Write-Host "Stage distribution: A=$countA, B=$countB, D=$countD, E=$countE, F=$countF (out of $($results.Count))"
