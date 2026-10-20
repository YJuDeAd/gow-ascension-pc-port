# tools/run_port.ps1 — God of War: Ascension Port Runner
param(
    [string]$ElfPath = ".\vfs\PS3_GAME\USRDIR\EBOOT.ELF",
    [int]$TimeoutSeconds = 15,
    [switch]$LiveDraw,
    [switch]$Interactive,
    [int]$Width = 0,
    [int]$Height = 0,
    [string]$OutLog = "build/run_port_stdout.log",
    [string]$ErrLog = "build/run_port_stderr.log"
)

$ErrorActionPreference = "Stop"
Remove-Item env:PPU_OPD_RECOVER -ErrorAction SilentlyContinue
Remove-Item env:PPU_NOOP_BAD_VCALL -ErrorAction SilentlyContinue

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
Write-Host "[run_port] Configured Render Resolution: ${cfgWidth}x${cfgHeight} (Scale=${cfgScale}x, VSync=${cfgVsync})"

if ($cfgLiveDraw) {
    Write-Host "[run_port] Enabling LiveDraw (D3D12 RSX) profile..."
    $env:RSX_LIVE_DRAW = "1"
    $env:GCM_RECDBG = "1"
    $env:GCM_REFLOG = "1"
    $env:LD_TRACE_PSO = "1"
    $env:GCM_SEMA_ACQUIRE = "0"
    $env:SPU_HOST_DEPTH_MAX = "100000"
    $env:SPU_INTERP_UNLIFTED = "1"
    $env:SPU_DMA_LAX = "1"
    $env:YDKJ_INJECT_PAD = $cfgAutoPad
    $env:GOW_PAD_AUTOSTART = $cfgAutoPad
    $env:PAD_ALWAYS_CHANGE = "1"
    $env:PS3_HDD0_ROOT = "vfs"
    Remove-Item env:PS3_HDD1_ROOT -ErrorAction SilentlyContinue
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

# 3. Check executable
$exe = ".\build\gow_ascension.exe"
if (!(Test-Path $exe)) {
    Write-Error "[run_port] Executable $exe not found! Run cmake --build build first."
    exit 1
}

# 4. Launch executable with timeout capture
Write-Host "[run_port] Launching $exe with ELF=$ElfPath (Timeout=${TimeoutSeconds}s)..."
$proc = Start-Process -FilePath $exe -ArgumentList $ElfPath -PassThru -RedirectStandardOutput $OutLog -RedirectStandardError $ErrLog

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

Write-Host "[run_port] Execution logs saved to $OutLog and $ErrLog"
