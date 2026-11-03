# Headless regression for the actual FIFO packet-drain/recovery methods.
. (Join-Path $PSScriptRoot "env.ps1")
$test = Join-Path $Cfg.PS3KIT_DIR "gpu\tests\test_fifo_recovery.py"
$testBuild = Join-Path $Cfg.WORK_DIR "fifo-tests"
cmd /c "`"$($Cfg.VCVARS)`" >nul && python `"$test`" --cxx clang-cl --build-dir `"$testBuild`""
exit $LASTEXITCODE
