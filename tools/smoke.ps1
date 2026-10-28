# tools/smoke.ps1 — Automated Smoke Test for God of War: Ascension Recompilation
# Usage: .\tools\smoke.ps1 -Seconds 90 -Cache cold|warm|keep

param(
    [int]$Seconds = 90,
    [ValidateSet("cold", "warm", "keep")][string]$Cache = "keep",
    [ValidateSet("off", "tiny", "full")][string]$FiosCache = "off"
)

$ErrorActionPreference = "Stop"

# 1. Guardrail: Ensure free disk space > 15 GB
$drive = Get-PSDrive C
if ($drive.Free -lt 15GB) {
    Write-Error "[smoke] Guardrail violated: Free disk on C: is below 15 GB ($([math]::Round($drive.Free/1GB, 2)) GB free). Aborting!"
    exit 1
}

# 2. FIOS Cache Mode Handling
$fiosCacheDir = "vfs\cache\BLES00000\fios"
if ($FiosCache) {
    $env:GOW_FIOS_CACHE = $FiosCache
}
Write-Host "[smoke] GOW_FIOS_CACHE is set to: $env:GOW_FIOS_CACHE"
if ($Cache -eq "cold") {
    Write-Host "[smoke] Cache mode: COLD. Purging vfs\cache..."
    if (Test-Path "vfs\cache") {
        Remove-Item -Path "vfs\cache\*" -Force -Recurse -ErrorAction SilentlyContinue
    }
    if ($env:GOW_FIOS_CACHE -eq "tiny") {
        New-Item -ItemType Directory -Path $fiosCacheDir -Force | Out-Null
        Copy-Item -Path "vfs\cache_tiny\cache.idx" -Destination "$fiosCacheDir\cache.idx" -Force
        New-Item -ItemType File -Path "$fiosCacheDir\cache.dat" -Force | Out-Null
        Write-Host "[smoke] Tiny cache populated: cache.idx (3584 B) + empty cache.dat"
    }
} elseif ($Cache -eq "warm") {
    Write-Host "[smoke] Cache mode: WARM. Reusing existing cache files in $fiosCacheDir."
} else {
    Write-Host "[smoke] Cache mode: KEEP. Cache left untouched."
}

# 3. Check Executable
$exe = "build\gow_ascension.exe"
if (!(Test-Path $exe)) {
    Write-Error "[smoke] Executable $exe not found! Please build first (cmake --build build --config Release)."
    exit 1
}

$elf = "vfs\PS3_GAME\USRDIR\EBOOT.ELF"
if (!(Test-Path $elf)) {
    Write-Error "[smoke] Game ELF $elf not found!"
    exit 1
}

# 4. Clean Frames Directory for Clean Count
$framesDir = "build\frames"
New-Item -ItemType Directory -Path $framesDir -Force | Out-Null
Get-ChildItem -Path $framesDir -Filter "*.ppm" -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# 5. Collect Pre-run Metadata via Helper
$preInfo = (python tools/smoke_helper.py pre).Trim()

# 6. Configure Environment
$env:GOW_NO_WWS_HACK = "1"
$env:RSX_LIVE_DRAW = "1"
$env:SPU_INTERP_STATS = "10"
$env:PS3_HLE_TOP = "10"
$env:SPURS_WLDUMP = "10"
$env:SPU_INTERP_UNLIFTED = "1"
$env:SPU_DMA_LAX = "1"
$env:GOW_PAD_AUTOSTART = "1"
$env:PAD_ALWAYS_CHANGE = "1"
$env:PS3_HDD0_ROOT = "vfs"
$env:PS3_HDD1_ROOT = (Resolve-Path "vfs").Path
$env:AUDIO_RATE = "1"
$env:AUDIO_PEAK = "1"
$env:LD_FRAME_DUMP = "build/frames"
$env:LD_FRAME_DUMP_EVERY = "60"

if (Test-Path "E:\PS3_GAME\USRDIR\GAME.PSARC") {
    $env:PS3_VFS_ROOT = "E:"
} elseif (Test-Path "D:\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso") {
    Mount-DiskImage -ImagePath "D:\God of War - Ascension (Asia) (En,Zh,Ko).dec.iso" -StorageType ISO | Out-Null
    if (Test-Path "E:\PS3_GAME\USRDIR\GAME.PSARC") {
        $env:PS3_VFS_ROOT = "E:"
    }
}

if (Test-Path "mods\skip-intro") {
    $env:PS3_MODS = (Resolve-Path "mods\skip-intro").Path
}

# Ensure GOW_HOOKS is not active unless explicitly set outside
if (-not $env:GOW_HOOKS) {
    Remove-Item env:GOW_HOOKS -ErrorAction SilentlyContinue
}

# 7. Configure Log Paths
$logDir = "build\logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$stdoutLog = "$logDir\smoke_${timestamp}_stdout.log"
$stderrLog = "$logDir\smoke_${timestamp}_stderr.log"

Write-Host "[smoke] Launching $exe (Seconds=${Seconds}s, Cache=$Cache)..."
Write-Host "[smoke] Stdout -> $stdoutLog"
Write-Host "[smoke] Stderr -> $stderrLog"

# 8. Start Game Process
$proc = Start-Process -FilePath $exe -ArgumentList $elf -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

$elapsed = 0
while (!$proc.HasExited -and $elapsed -lt $Seconds) {
    Start-Sleep -Seconds 1
    $elapsed++
    if ($elapsed % 15 -eq 0) {
        Write-Host "[smoke] Elapsed ${elapsed}s / ${Seconds}s..."
    }
}

if (!$proc.HasExited) {
    Write-Host "[smoke] Run reached timeout (${Seconds}s); terminating process gracefully."
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "[smoke] Process exited with code: $($proc.ExitCode) at elapsed ${elapsed}s."
}

# Small pause to allow file handles to flush
Start-Sleep -Seconds 2

# 9. Post-run Analysis & CSV Recording via Helper
python tools/smoke_helper.py post $timestamp $Cache $preInfo $stdoutLog $stderrLog $framesDir
if ($LASTEXITCODE -ne 0) {
    Write-Error "[smoke] FAILED: Effective environment defaults hash is empty or da39a3ee...!"
    exit 2
}
