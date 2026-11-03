# Build RPCS3's RSX library (the toolkit's gpu\) into GPU_BUILD_DIR with MSVC
# (cl; RPCS3's code does not build with clang-cl) and the Vulkan SDK. Settings
# from .env. Then set RPCS3_RSX=1 in .env and run
# .\scripts\windows\build.ps1 -Configure.
#
#   .\scripts\windows\build-gpu.ps1              # log: build\logs\gpu-build.log
#   .\scripts\windows\build-gpu.ps1 -Configure   # run CMake again first
#
# A first build takes a few minutes (RPCS3's RSX and glslang).
param([switch]$Configure)

. (Join-Path $PSScriptRoot "env.ps1")
$Gpu   = Join-Path $Cfg.PS3KIT_DIR "gpu"
$Build = $Cfg.GPU_BUILD_DIR
$Logs  = Join-Path $Root "build\logs"
New-Item -ItemType Directory -Force $Logs | Out-Null
$Log = Join-Path $Logs "gpu-build.log"

if (-not (Test-Path (Join-Path $Gpu "third_party\glslang\CMakeLists.txt"))) {
    Write-Host "glslang/asmjit missing: git -C `"$($Cfg.PS3KIT_DIR)`" submodule update --init --depth 1"
    exit 1
}
if (-not $Cfg.VULKAN_SDK) { Write-Host "VULKAN_SDK is not set (install the Vulkan SDK or set it in .env)"; exit 1 }

if ($Configure -or -not (Test-Path (Join-Path $Build "build.ninja"))) {
    $ConfLog = Join-Path $Logs "gpu-configure.log"
    Write-Host "configuring ps3rsx -> $Build (log: $ConfLog)"
    cmd /c "`"$($Cfg.VCVARS)`" >nul && cmake -S `"$Gpu`" -B `"$Build`" -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release" 2>&1 |
        Out-File -Encoding utf8 $ConfLog
    if ($LASTEXITCODE -ne 0) { Get-Content $ConfLog -Tail 20; exit $LASTEXITCODE }
}

$jobs = if ($Cfg.JOBS) { "-j $($Cfg.JOBS)" } else { "" }
$sw = [Diagnostics.Stopwatch]::StartNew()
cmd /c "`"$($Cfg.VCVARS)`" >nul && ninja -C `"$Build`" $jobs ps3rsx_flip_smoke" 2>&1 | Out-File -Encoding utf8 $Log
$code = $LASTEXITCODE
$sw.Stop()

$errors = Select-String -Path $Log -Pattern "error C|error:|FAILED:|fatal error|LNK\d{4}" | Select-Object -First 15
Write-Host ("gpu build {0} in {1:mm\:ss}  (log: {2})" -f $(if ($code -eq 0) { "OK" } else { "FAILED" }), $sw.Elapsed, $Log)
if ($errors) { $errors | ForEach-Object { Write-Host "  $($_.Line)" } }
Write-Host ("last line: " + (Get-Content $Log -Tail 1))
exit $code
