# Compare production per-hop hooks without launching the game.
. (Join-Path $PSScriptRoot "env.ps1")
$test = Join-Path $Cfg.PS3KIT_DIR "runtime\spu\tests\test_spu_launch.py"
$testBuild = Join-Path $Cfg.WORK_DIR "spu-launch-tests"
cmd /c "`"$($Cfg.VCVARS)`" >nul && python `"$test`" --build-dir `"$testBuild`""
exit $LASTEXITCODE
