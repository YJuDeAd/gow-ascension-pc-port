# tools/run_port.ps1 — God of War: Ascension Port Runner

param(

    [string]$ElfPath = ".\vfs\PS3_GAME\USRDIR\EBOOT.ELF",

    [int]$TimeoutSeconds = 120,

    [switch]$LiveDraw,

    [switch]$Interactive,

    [int]$Width = 0,

    [int]$Height = 0,

    [string]$OutLog = "",

    [string]$ErrLog = "",

    [switch]$AllowWwsHack,

    [switch]$Quiet,

    [switch]$DumpFrames,

    [switch]$NoMods,

    [switch]$Menu

)



if (-not $AllowWwsHack) {

    $env:GOW_NO_WWS_HACK = "1"

    Write-Host "[run_port] GOW_NO_WWS_HACK=1 enabled (clean lifted WWS execution)"

} else {

    Remove-Item env:GOW_NO_WWS_HACK -ErrorAction SilentlyContinue

    Write-Host "[run_port] GOW_NO_WWS_HACK disabled (artificial unblockers active)"

}



if ($Menu) {

    $env:PAD_SCRIPT = "14:0x6008,18:0x6008,22:0x6008,26:0x6008,30:0x6008,35:0x6008,40:0x6008,45:0x6008,50:0x6008,55:0x6008,60:0x6008,70:0x6008,80:0x6008,90:0x6008,100:0x6008"

    Write-Host "[run_port] Menu mode: PAD_SCRIPT=14:0x4000,15:0x0000 (skip cinematic at 14s)"

}



$ErrorActionPreference = "Stop"

Remove-Item env:PPU_OPD_RECOVER -ErrorAction SilentlyContinue

Remove-Item env:PPU_NOOP_BAD_VCALL -ErrorAction SilentlyContinue



if ($DumpFrames) {

    New-Item -ItemType Directory -Path "build/frames" -Force | Out-Null

    Get-ChildItem -Path "build/frames" -Filter "*.ppm" -File | Remove-Item -Force -ErrorAction SilentlyContinue

    $env:LD_FRAME_DUMP = "build/frames"

    $env:LD_FRAME_DUMP_EVERY = "20"

    $env:GOW_PROFILE = "1"

    Write-Host "[run_port] Frame dumping enabled: LD_FRAME_DUMP=build/frames, LD_FRAME_DUMP_EVERY=20"

}



# 0. Parse config.toml if present

$cfgPath = ".\config.toml"

$cfgWidth = 1280

$cfgHeight = 720

$cfgVsync = "on"

$cfgScale = 1

$cfgDeadzone = 7849

$cfgAudioRate = "1"

$cfgAudioPeak = "1"

$cfgLiveDraw = $true

$cfgAutoPad = "1"



if (Test-Path $cfgPath) {

    Write-Host "[run_port] Loading settings from $cfgPath..."

    $content = Get-Content $cfgPath

    foreach ($line in $content) {

        $l = $line.Trim()

        if ($l -match '^width\s*=\s*(\d+)') { $cfgWidth = [int]$matches[1] }

        if ($l -match '^height\s*=\s*(\d+)') { $cfgHeight = [int]$matches[1] }

        if ($l -match '^vsync\s*=\s*"([^"]+)"') { $cfgVsync = $matches[1] }

        if ($l -match '^scale_factor\s*=\s*(\d+)') { $cfgScale = [int]$matches[1] }

        if ($l -match '^deadzone\s*=\s*(\d+)') { $cfgDeadzone = [int]$matches[1] }

        if ($l -match '^log_audio_rate\s*=\s*(true|false)') { $cfgAudioRate = if ($matches[1] -eq "true") { "1" } else { "0" } }

        if ($l -match '^log_audio_peak\s*=\s*(true|false)') { $cfgAudioPeak = if ($matches[1] -eq "true") { "1" } else { "0" } }

        if ($l -match '^live_draw\s*=\s*(true|false)') { $cfgLiveDraw = ($matches[1] -eq "true") }

        if ($l -match '^auto_pad_start\s*=\s*(true|false)') { $cfgAutoPad = if ($matches[1] -eq "true") { "1" } else { "0" } }

    }

}



# Command line overrides

if ($Interactive) {

    $cfgLiveDraw = $true

    $cfgAutoPad = "0"

    $TimeoutSeconds = 0

}

if ($Width -gt 0) { $cfgWidth = $Width }

if ($Height -gt 0) { $cfgHeight = $Height }

if ($LiveDraw) { $cfgLiveDraw = $true }



# Configure resolution environment variables

$env:GOW_RSX_W = "$cfgWidth"

$env:GOW_RSX_H = "$cfgHeight"

if ($cfgVsync -eq "off") {

    $env:LD_PRESENT_INTERVAL = "0"

} else {

    $env:LD_PRESENT_INTERVAL = "1"

}

Write-Host "[run_port] Configured Render Resolution: ${cfgWidth}x${cfgHeight} (Scale=${cfgScale}x, VSync=${cfgVsync}, LD_PRESENT_INTERVAL=$env:LD_PRESENT_INTERVAL)"



if ($cfgLiveDraw) {

    Write-Host "[run_port] Enabling LiveDraw (D3D12 RSX) profile..."

    $env:RSX_LIVE_DRAW = "1"

    if (-not $Quiet) {

        $env:GCM_RECDBG = "1"

        $env:GCM_REFLOG = "1"

        $env:LD_TRACE_PSO = "1"

        $env:LD_ALIAS_DBG = "1"

    } else {

        Remove-Item env:GCM_RECDBG -ErrorAction SilentlyContinue

        Remove-Item env:GCM_REFLOG -ErrorAction SilentlyContinue

        Remove-Item env:LD_TRACE_PSO -ErrorAction SilentlyContinue

        Remove-Item env:LD_ALIAS_DBG -ErrorAction SilentlyContinue

        Write-Host "[run_port] Quiet mode: GCM_RECDBG, GCM_REFLOG, LD_TRACE_PSO, LD_ALIAS_DBG unset"

    }

    $env:GCM_SEMA_ACQUIRE = "1"

    $env:SPU_HOST_DEPTH_MAX = "100000"

    $env:SPU_INTERP_UNLIFTED = "1"

    $env:SPU_DMA_LAX = "1"

    $env:YDKJ_INJECT_PAD = $cfgAutoPad

    $env:GOW_PAD_AUTOSTART = $cfgAutoPad

    $env:PAD_ALWAYS_CHANGE = "1"

    $env:PS3_HDD0_ROOT = "vfs"

    $env:PS3_HDD1_ROOT = (Resolve-Path "vfs").Path

    $env:FS_CALLER = "1"

    $env:YDKJ_PAD_BTN = "0x0008"

    $env:YDKJ_PAD_DELAY = "5"

    $env:LD_ALIAS_DBG = "1"

    $env:AUDIO_RATE = $cfgAudioRate

    $env:AUDIO_PEAK = $cfgAudioPeak

    $env:PS3_NULL_READ_STACK = "1"

    $env:PS3_CALLTRACE = "20"

    $env:PS3_CALLTRACE_LR = "0x00385108"

    Remove-Item env:PPU_OPD_RECOVER -ErrorAction SilentlyContinue

    Remove-Item env:PPU_NOOP_BAD_VCALL -ErrorAction SilentlyContinue

    Write-Host "[run_port] Environment: RSX_LIVE_DRAW=$env:RSX_LIVE_DRAW, GOW_PAD_AUTOSTART=$env:GOW_PAD_AUTOSTART, PS3_HDD0_ROOT=$env:PS3_HDD0_ROOT, PS3_HDD1_ROOT=$env:PS3_HDD1_ROOT, AUDIO_RATE=$env:AUDIO_RATE, AUDIO_PEAK=$env:AUDIO_PEAK"

}



# 1. Ensure decrypted ISO is mounted

$isoPath = "D:\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso"

if (!(Test-Path $isoPath)) {

    $isoPath = "C:\IfItWorksItWorks\Project\gowassension\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso"

}

if (!(Test-Path "E:\PS3_GAME\USRDIR\GAME.PSARC")) {

    Write-Host "[run_port] Mounting decrypted ISO to virtual drive E:..."

    Mount-DiskImage -ImagePath $isoPath -StorageType ISO | Out-Null

    Start-Sleep -Seconds 2

}



if (!(Test-Path "E:\PS3_GAME\USRDIR\GAME.PSARC")) {

    Write-Error "[run_port] Failed to verify E:\PS3_GAME\USRDIR\GAME.PSARC after mount!"

    exit 1

}



# 2. Configure VFS root environment variable

$env:PS3_VFS_ROOT = "E:"

Write-Host "[run_port] Configured PS3_VFS_ROOT = $env:PS3_VFS_ROOT"

if (-not $NoMods -and (Test-Path "mods\skip-intro")) {

    $modsDir = (Resolve-Path "mods\skip-intro").Path

    $env:PS3_MODS = $modsDir

    Write-Host "[run_port] Configured PS3_MODS = $env:PS3_MODS"

} else {

    Remove-Item env:PS3_MODS -ErrorAction SilentlyContinue

    Write-Host "[run_port] PS3_MODS unset (intro logo screens will play)"

}



# Clean any zero-filled FIOS cache.idx left by aborted runs

Get-ChildItem -Path "vfs\cache" -Recurse -Filter "cache.idx" -ErrorAction SilentlyContinue | ForEach-Object {

    $idxPath = $_.FullName

    $bytes = [System.IO.File]::ReadAllBytes($idxPath)

    if ($bytes.Length -ge 64) {

        $hasNonZero = $false

        for ($i = 0; $i -lt 64; $i++) {

            if ($bytes[$i] -ne 0) { $hasNonZero = $true; break }

        }

        if (-not $hasNonZero) {

            Write-Host "[run_port] Zero-filled FIOS cache detected at $idxPath; purging broken cache directory..."

            $parentDir = (Get-Item $idxPath).Directory.FullName

            Remove-Item -Recurse -Force $parentDir -ErrorAction SilentlyContinue

        }

    }

}



# 3. Check executable

$exe = ".\build\gow_ascension.exe"

if (!(Test-Path $exe)) {

    Write-Error "[run_port] Executable $exe not found! Run cmake --build build first."

    exit 1

}



# 4. Configure timestamped logging

$logDir = "build/logs"

if (!(Test-Path $logDir)) {

    New-Item -ItemType Directory -Path $logDir -Force | Out-Null

}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

$tsOutLog = "$logDir/run_${timestamp}_stdout.log"

$tsErrLog = "$logDir/run_${timestamp}_stderr.log"



$targetOutLog = if (![string]::IsNullOrEmpty($OutLog)) { $OutLog } else { $tsOutLog }

$targetErrLog = if (![string]::IsNullOrEmpty($ErrLog)) { $ErrLog } else { $tsErrLog }



# 5. Launch executable with timeout capture

Write-Host "[run_port] Launching $exe with ELF=$ElfPath (Timeout=${TimeoutSeconds}s)..."

$proc = Start-Process -FilePath $exe -ArgumentList $ElfPath -PassThru -RedirectStandardOutput $targetOutLog -RedirectStandardError $targetErrLog



if ($TimeoutSeconds -gt 0) {

    $elapsed = 0

    while (!$proc.HasExited -and $elapsed -lt $TimeoutSeconds) {

        Start-Sleep -Seconds 1

        $elapsed++

    }

    if (!$proc.HasExited) {

        Write-Host "[run_port] Process reached timeout (${TimeoutSeconds}s); stopping gracefully for log analysis."

        Stop-Process -Id $proc.Id -Force

    } else {

        Write-Host "[run_port] Process exited with code: $($proc.ExitCode)"

    }

} else {

    Write-Host "[run_port] Running interactively (no timeout). Close the game window to stop."

    $proc.WaitForExit()

    Write-Host "[run_port] Process exited with code: $($proc.ExitCode)"

}



if ($targetOutLog -ne "build/run_port_stdout.log" -and (Test-Path $targetOutLog)) {

    Copy-Item -Path $targetOutLog -Destination "build/run_port_stdout.log" -Force

}

if ($targetErrLog -ne "build/run_port_stderr.log" -and (Test-Path $targetErrLog)) {

    Copy-Item -Path $targetErrLog -Destination "build/run_port_stderr.log" -Force

}



Write-Host "[run_port] Execution logs saved to $targetOutLog and $targetErrLog"



# Prune build/logs to keep only 3 newest runs

$runFiles = Get-ChildItem -Path "$logDir" -Filter "run_*_*.log" -File

$runGroups = $runFiles | Group-Object { $_.BaseName -replace '_(stdout|stderr)$', '' } | Sort-Object Name -Descending

if ($runGroups.Count -gt 3) {

    $runGroups | Select-Object -Skip 3 | ForEach-Object {

        $_.Group | Remove-Item -Force

    }

}

# Old frames retained if dumped during this run

