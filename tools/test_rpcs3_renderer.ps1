$argsList = @("vfs\PS3_GAME\USRDIR\EBOOT.ELF", "--renderer=rpcs3-rsx")
$proc = Start-Process -FilePath "build\gow_ascension.exe" -ArgumentList $argsList -PassThru -RedirectStandardError "scratch\rpcs3_test_stderr.log" -RedirectStandardOutput "scratch\rpcs3_test_stdout.log"
Start-Sleep -Seconds 8
if (!$proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
}
Start-Sleep -Seconds 1
Get-Content "scratch\rpcs3_test_stderr.log" | Select-String -Pattern "rpcs3|renderer|\[rsx\]" | Select-Object -First 20
