# Build RPCS3's RSX library (the toolkit's gpu\) into GPU_BUILD_DIR
# Settings from .env or default paths.
#
#   .\scripts\windows\build-gpu.ps1              # log: build\logs\gpu-build.log
#   .\scripts\windows\build-gpu.ps1 -Configure   # run CMake again first

param([switch]$Configure)

$PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$envFile = Join-Path $Root ".env"
$Cfg = @{}
if (Test-Path $envFile) {
    foreach ($line in Get-Content $envFile) {
        $l = $line.Trim()
        if (-not $l -or $l.StartsWith("#")) { continue }
        $eq = $l.IndexOf("=")
        if ($eq -lt 1) { continue }
        $Cfg[$l.Substring(0, $eq).Trim()] = $l.Substring($eq + 1).Trim().Trim('"').Trim("'")
    }
}

$Gpu = if ($Cfg.PS3KIT_DIR) { Join-Path $Cfg.PS3KIT_DIR "gpu" } else { Join-Path $Root "ps3recomp\gpu" }
$Build = if ($Cfg.GPU_BUILD_DIR) { $Cfg.GPU_BUILD_DIR } else { Join-Path $Root "work\gpu-build" }
if (-not [IO.Path]::IsPathRooted($Build)) { $Build = Join-Path $Root $Build }

$Logs  = Join-Path $Root "build\logs"
New-Item -ItemType Directory -Force $Logs | Out-Null
$Log = Join-Path $Logs "gpu-build.log"

$vcvars = $Cfg.VCVARS
$useVcvars = ($vcvars -and (Test-Path $vcvars))

$llvmMingw = "C:/Users/ROBIN/AppData/Local/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/llvm-mingw-20260908-ucrt-x86_64/bin"
$clangC = if (Test-Path "$llvmMingw/clang.exe") { "$llvmMingw/clang.exe" } else { "clang" }
$clangCxx = if (Test-Path "$llvmMingw/clang++.exe") { "$llvmMingw/clang++.exe" } else { "clang++" }

if ($Configure -or -not (Test-Path (Join-Path $Build "build.ninja"))) {
    $ConfLog = Join-Path $Logs "gpu-configure.log"
    Write-Host "configuring ps3rsx -> $Build (log: $ConfLog)"
    New-Item -ItemType Directory -Force $Build | Out-Null
    if ($useVcvars) {
        cmd /c "`"$vcvars`" >nul && cmake -S `"$Gpu`" -B `"$Build`" -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release" 2>&1 |
            Out-File -Encoding utf8 $ConfLog
    } else {
        cmake -S "$Gpu" -B "$Build" -G Ninja -DCMAKE_C_COMPILER="$clangC" -DCMAKE_CXX_COMPILER="$clangCxx" -DCMAKE_BUILD_TYPE=Release 2>&1 |
            Out-File -Encoding utf8 $ConfLog
    }
    if ($LASTEXITCODE -ne 0) { Get-Content $ConfLog -Tail 20; exit $LASTEXITCODE }
}

$jobs = if ($Cfg.JOBS) { "-j $($Cfg.JOBS)" } else { "" }
$sw = [Diagnostics.Stopwatch]::StartNew()
if ($useVcvars) {
    cmd /c "`"$vcvars`" >nul && ninja -C `"$Build`" $jobs ps3rsx_flip_smoke" 2>&1 | Out-File -Encoding utf8 $Log
} else {
    ninja -C "$Build" $jobs ps3rsx_flip_smoke 2>&1 | Out-File -Encoding utf8 $Log
}
$code = $LASTEXITCODE
$sw.Stop()

$errors = Select-String -Path $Log -Pattern "error C|error:|FAILED:|fatal error|LNK\d{4}" | Select-Object -First 15
Write-Host ("gpu build {0} in {1:mm\:ss}  (log: {2})" -f $(if ($code -eq 0) { "OK" } else { "FAILED" }), $sw.Elapsed, $Log)
if ($errors) { $errors | ForEach-Object { Write-Host "  $($_.Line)" } }
Write-Host ("last line: " + (Get-Content $Log -Tail 1))
exit $code
